/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_adc.h"
#include "board_host_command.h"
#include "chipset.h"
#include "config.h"
#include "console.h"
#include "common.h"
#include "customized_shared_memory.h"
#include "cypress_pd_common.h"
#include "diagnostics.h"
#include "espi.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "driver/ina2xx.h"
#include "lpc.h"
#include "power.h"
#include "power_sequence.h"
#include "power_monitor.h"
#include "port80.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "wol.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

#define IN_VR_PGOOD POWER_SIGNAL_MASK(X86_VR_PG)
#define IN_VS_POWER POWER_SIGNAL_MASK(X86_VS_POWER)
#define IN_VALW_PGOOD POWER_SIGNAL_MASK(X86_3VALW_PG)

#define NPCX_ESPI_VWEVSM_ADDR ((volatile uint32_t *)0x4000a160)

/**
 * After EC turns off the susp#, the VS power good will deassert in 2ms.
 * If the hardware happens something wrong, the VS power good does not
 * deassert in 20ms, EC will force back to g3 and turn off all power rail.
 */
#define TIMEOUT_VS_POWER_TURN_OFF (20 * MSEC)

#define POWER_12V_LED_BLINKING_PERIOD (500 * MSEC)
#define POWER_12V_LED_BLINKING_SECOND(x) ((x * SECOND) / POWER_12V_LED_BLINKING_PERIOD)

/*
 * Time to wait until turning off the power supply in low power
 * usage (like suspend).
 *
 * _MIN start at this timeout,
 * After every low-power entry we'll *= by _INCREASE_FACTOR.
 * for a max of _MAX
 */
#define TIMEOUT_5VSB_MIN (1 * MINUTE)
#define TIMEOUT_5VSB_INCREASE_FACTOR 2
#define TIMEOUT_5VSB_MAX (30 * MINUTE)
static int timeout_5vsb = TIMEOUT_5VSB_MIN;

static bool power_s5_up;		/* Chipset is sequencing up or down */
static int force_shutdown_flags;
static int d3cold_is_entry;	/* check the d3cold status */
static int force_enable_psu = 0;

static void system_check_ssd_status(void);

static bool power_enable_psu(bool enable)
{
	int board_version = board_get_version();

	/* EC cannot control the ps_on pin on EVT mainboard */
	if (board_version <= BOARD_VERSION_4)
		return false;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ps_on), enable);

	if (enable) {
		CPRINTS("PS_ON was sent, waiting for POK");
		/**
		 * Wait for the PSU power good.
		 * According to the waveform, EC needs to delay 500ms to
		 * wait the pok signal to be asserted after enable the PSU.
		 */
		if (power_wait_signals(IN_VALW_PGOOD))
			return false;
	}

	return true;
}

static void power_usb_huba_reset(bool enable)
{
	int board_version = board_get_version();

	/**
	 * huba_rst pin connects to GPIOD5 at EVT phase and connects to GPIOF0
	 * at DVT version.
	 * Use board_version to control the huba_rst pin to ensure we don't let
	 * EVT mainboard be broken.
	 */
	if (board_version <= BOARD_VERSION_4)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ps_on), enable);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_huba_rst_l), enable);
}

static void peripheral_power_startup(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hubc_rst_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pcieslot_rst_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usbc_hub_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usba_hub_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lan_rst_l), 1);
	power_usb_huba_reset(1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, peripheral_power_startup, HOOK_PRIO_DEFAULT);

static void power_resume_lan_sequence(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_aux_on), 1);
	k_msleep(120);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lan_rst_l), 1);
}

static void power_suspend_lan_sequence(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_aux_on), 0);
	k_msleep(1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lan_rst_l), 0);
}

static void peripheral_power_resume(void)
{
	power_resume_lan_sequence();
}

static void peripheral_power_shutdown(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hubc_rst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pcieslot_rst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usbc_hub_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usba_hub_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_lan_rst_l), 0);
	power_usb_huba_reset(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, peripheral_power_shutdown, HOOK_PRIO_DEFAULT);

static void peripheral_power_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 0);

	/* should not turn off the WoL signal if WoL is enabled */
	if (!wake_on_lan_is_enabled())
		power_suspend_lan_sequence();
}

/*
 * We need to keep pch power to wait SLP_S5 signal for the below cases:
 *
 * 1. Customer testing tool
 * 2. There is a type-c USB input deck connect on the unit
 */
static int should_stay_powered_on(void)
{
	int wake_source = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT);

	if (wake_source & (RTCWAKE | USBWAKE))
		return true;
	else
		return false;

}

/*
 * Backup copies of SCI mask to preserve across S0ix suspend/resume
 * cycle. If the host uses S0ix, BIOS is not involved during suspend and resume
 * operations and hence SCI masks are programmed only once during boot-up.
 *
 * These backup variables are set whenever host expresses its interest to
 * enter S0ix and then lpc_host_event_mask for SCI are cleared. When
 * host resumes from S0ix, masks from backup variables are copied over to
 * lpc_host_event_mask for SCI.
 */
static host_event_t backup_sci_mask;

/*
 * Clear host event masks for SCI when host is entering S0ix. This is
 * done to prevent any SCI interrupts when the host is in suspend. Since
 * BIOS is not involved in the suspend path, EC needs to take care of clearing
 * these masks.
 */
static void lpc_s0ix_suspend_clear_masks(void)
{
	backup_sci_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, SCI_HOST_WAKE_EVENT_MASK);
}

/*
 * Restore host event masks for SCI when host exits S0ix. This is done
 * because BIOS is not involved in the resume path and so EC needs to restore
 * the masks from backup variables.
 */
static void lpc_s0ix_resume_restore_masks(void)
{
	/*
	 * No need to restore SCI masks if both backup_sci_mask are zero.
	 * This indicates that there was a failure to enter S0ix
	 * and hence SCI masks were never backed up.
	 */
	if (!backup_sci_mask)
		return;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, backup_sci_mask);

	backup_sci_mask = 0;
}

/* BIOS clear event in open or shutdon, to check EC necessary wake events */
static void power_clear_wake_event(uint8_t wake_events)
{
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT) &= ~wake_events;
}

void power_state_clear(int state)
{
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_STATE) &= ~state;
}

void power_s5_up_control(int control)
{
	power_s5_up = control;
}

void clear_power_flags(void)
{
	/**
	 * When system reboot and go into setup menu, we need to set the power_s5_up flag
	 * to wait SLP_S5 and SLP_S3 signal to boot into OS.
	 */
	power_s5_up_control(1);

	power_state_clear(EC_PS_ENTER_S4 | EC_PS_RESUME_S4 |
		EC_PS_ENTER_S5 | EC_PS_RESUME_S5);

	d3cold_is_entry = 0;
}

#ifdef CONFIG_PLATFORM_EC_POWERSEQ_S0IX
/*
 * Backup copies of SCI mask to preserve across S0ix suspend/resume
 * cycle. If the host uses S0ix, BIOS is not involved during suspend and resume
 * operations and hence SCI masks are programmed only once during boot-up.
 *
 * These backup variables are set whenever host expresses its interest to
 * enter S0ix and then lpc_host_event_mask for SCI are cleared. When
 * host resumes from S0ix, masks from backup variables are copied over to
 * lpc_host_event_mask for SCI.
 */
static int enter_ms_flag;
static int resume_ms_flag;
static int system_in_s0ix;

static int check_s0ix_statsus(void)
{
	int power_status;
	int clear_flag;

	/* check power state S0ix flags */
	if (chipset_in_state(CHIPSET_STATE_ON) || chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		power_status = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_STATE);


		/**
		 * Sometimes PCH will set the enter and resume flag continuously
		 * so clear the EMI when we read the flag.
		 */
		if (power_status & EC_PS_ENTER_S0ix)
			enter_ms_flag++;

		if (power_status & EC_PS_RESUME_S0ix)
			resume_ms_flag++;

		clear_flag = power_status & (EC_PS_ENTER_S0ix | EC_PS_RESUME_S0ix);

		power_state_clear(clear_flag);

		if (resume_ms_flag)
			return CS_EXIT_S0ix;

		if (enter_ms_flag)
			return CS_ENTER_S0ix;
	}
	return 0;
}

static void power_clear_s0ix_flag(void)
{
	resume_ms_flag = 0;
	enter_ms_flag = 0;
	system_in_s0ix = 0;
}

void s0ix_status_handle(void)
{
	int s0ix_state_change;

	s0ix_state_change = check_s0ix_statsus();

	if ((s0ix_state_change == CS_ENTER_S0ix) && chipset_in_state(CHIPSET_STATE_ON))
		task_wake(TASK_ID_CHIPSET);
	else if ((s0ix_state_change == CS_EXIT_S0ix) && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_TICK, s0ix_status_handle, HOOK_PRIO_DEFAULT);

int check_s0ix_status(void)
{
	return system_in_s0ix;
}

#endif

void chipset_reset(enum chipset_shutdown_reason reason)
{
	/* unused function, EC doesn't control GPIO_SYS_RESET_L */
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		CPRINTS("%s(%d)", __func__, reason);
		report_ap_reset(reason);
		force_shutdown_flags = 1;
		/**
		 * Force shutdown the system, clear the wake event and set the PD
		 * state to NO POWER to avoid the EC communicates with PD chip.
		 */
		cypd_update_chips_state(CCG_STATE_NO_POWER);
		power_clear_wake_event(RTCWAKE | USBWAKE);

		task_wake(TASK_ID_CHIPSET);
	}
}

enum power_state power_chipset_init(void)
{
	/* If we don't need to image jump to RW, always start at G3 state */
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	return POWER_G3;
}

/**
 * AMD recommended EC needs to check whether the system hangs or not,
 * If EC detect the system hangs, force reset the system then reboot again.
 */
#define VW_NO_READY 0
void system_hang_detect(void)
{
	int virtual_wire_ready = get_espi_virtual_wire_channel_status();

	if (virtual_wire_ready == VW_NO_READY) {
		board_reboot_ap_on_g3();
		chipset_force_shutdown(CHIPSET_RESET_HANG_REBOOT);
	}
}
DECLARE_DEFERRED(system_hang_detect);

void power_led2_blinking(void);
DECLARE_DEFERRED(power_led2_blinking);
void power_led2_blinking(void)
{
	static int tick;

	/* blink LED2 with 2 Hz */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led2_drv), !!(tick++ % 2));

	if (tick < POWER_12V_LED_BLINKING_SECOND(30))
		hook_call_deferred(&power_led2_blinking_data, POWER_12V_LED_BLINKING_PERIOD);
	else {
		/* Stop blinking the LED2 */
		tick = 0;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led2_drv), 0);
	}
}

static void power_check_12vb_apu(void)
{
	int voltage = ina2xx_get_voltage(0); /* Unit: mV */
	bool psu_is_supposed_to_be_on =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ps_on));

	/* LED should follow ps_on */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led2_drv),
					psu_is_supposed_to_be_on);
	/* Except when there's errors further on... */

	/**
	 * EC read the ina236 bus voltage register to monitor the
	 * 12VB_APU is present or not to control the debug led2.
	 */
	if (voltage < 5000) {
		/* Start blinking the LED2 30 seconds */
		power_led2_blinking();
		set_diagnostic(DIAGNOSTICS_12V_OK, 1);
	} else {
		/* Stop blinking if the 12V is detected */
		hook_call_deferred(&power_led2_blinking_data, -1);
	}
}

static void diagnostic_power_glitch_detect(void)
{
	if ((hw_diagnostics & (1 << DIAGNOSTICS_PSU_POK)) == 0 &&
		power_has_signals(IN_VALW_PGOOD) == 0)
		set_diagnostic(DIAGNOSTICS_POWER_GLITCH, 1);
}

void power_5vsb_enter(void)
{
	CPRINTS("current was low (<%dmA) for a long time, switching to 5vsb",
			INA236_MONITOR_5V_LOWER_CURRENT_MA);

	if (board_get_version() < BOARD_VERSION_9) {
		CPRINTS("...this board is too old to turn power supply off");
		return;
	}

	if (force_enable_psu) {
		CPRINTS("...but we want to force power supply on");
		return;
	}

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_s0ix), 1);

	k_msleep(20);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_rvsp_l), 0);

	k_msleep(10);
	power_enable_psu(0);

	/* Set the upper current limit */
	power_monitor_set_alert_current(INA236_IDX_PSU_5V,
			INA236_MONITOR_5V_UPPER_CURRENT_MA);

	power_check_12vb_apu();
}

static void power_5vsb_enter_deferred(void)
{
	power_5vsb_enter();
	timeout_5vsb *= TIMEOUT_5VSB_INCREASE_FACTOR;
	if (timeout_5vsb > TIMEOUT_5VSB_MAX)
		timeout_5vsb = TIMEOUT_5VSB_MAX;
}
DECLARE_DEFERRED(power_5vsb_enter_deferred);

bool power_5vsb_exit(void)
{
	CPRINTS("5vsb is not enough (>%dmA), turning on PSU",
			INA236_MONITOR_5V_UPPER_CURRENT_MA);

	if (!power_enable_psu(1))
		return false;

	if (board_get_version() < BOARD_VERSION_9) {
		CPRINTS("...power supply was already forced on because board is old");
		return true;
	}

	k_msleep(10);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_rvsp_l), 1);
	CPRINTS("switched to high power rails");

	k_msleep(10);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_s0ix), 0);

	/* Set the lower current limit */
	power_monitor_set_alert_current(INA236_IDX_PSU_5V,
			INA236_MONITOR_5V_LOWER_CURRENT_MA);

	power_check_12vb_apu();

	return true;
}

enum power_state power_handle_state(enum power_state state)
{
	int s5_exit_tries = 0;	/* For global reset to wait SLP_S5 signal de-asserts */
	if (run_diagnostics == 1)
		diagnostic_power_glitch_detect();

	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:

		k_msleep(10);

		/**
		 * If en_evsp_l does not set to high, the pok_l will not de-assert.
		 * So we don't care the power_enable_psu return value here.
		 * delay 400 ms to ensure the POK already turns on.
		 */
		power_enable_psu(1);

		if (board_get_version() >= BOARD_VERSION_8) {
			k_msleep(400);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_rvsp_l), 1);
		}

		if (power_wait_signals(IN_VALW_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
			return POWER_G3;
		}
		/* If pok_l is on make psu pok pass */
		set_diagnostic(DIAGNOSTICS_PSU_POK, 0);

		cypd_update_chips_state(CCG_STATE_POWER_ON);

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_1p2valw_pwren), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 1);

		/* Customizes power button out signal without PB task for powering on. */
		k_msleep(90);
		CPRINTS("PCH PBTN LOW");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		k_msleep(20);
		CPRINTS("PCH PBTN HIGH");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 1);

		/* Exit SOC G3 */
		CPRINTS("Exit SOC G3");
		/* check the 12vb_apu signal after turn on the PSU (ps_on = high) */
		power_check_12vb_apu();

		power_s5_up_control(1);
		return POWER_S5;

	case POWER_S5:

		if (force_shutdown_flags) {
			force_shutdown_flags = 0;
			return POWER_S5G3;
		}

		if (power_s5_up || stress_test_enable) {
			while (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 0) {
				if (task_wait_event(SECOND) == TASK_EVENT_TIMER) {
					if (++s5_exit_tries > ap_boot_delay) {
						CPRINTS("timeout waiting for S5 exit");
						/*
						 * TODO: RTC reset function
						 */
						ap_boot_delay = 9;
						s5_exit_tries = 0;
						stress_test_enable = 0;
						power_clear_wake_event(RTCWAKE | USBWAKE);
						set_diagnostic(DIAGNOSTICS_SLP_S5, 1);
						set_diagnostic(DIAGNOSTICS_SLP_S4, 1);
						/* SLP_S5 asserted, power down to G3S5 state */
						return POWER_S5G3;
					}
				}
			}
			/* Power up to next state */
			s5_exit_tries = 0;
			return POWER_S5S3;
		}

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 1) {
			s5_exit_tries = 0;
			/* Power up to next state */
			return POWER_S5S3;
		}

		break;

	case POWER_S5S3:
		/* enable the ssd2 power when the system power on from S5 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3:
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 1 &&
			!force_shutdown_flags) {

			/* still in s0ix state */
			if (system_in_s0ix)
				return POWER_S3S0ix;

			/* Power up to next state */
			k_msleep(10);
			return POWER_S3S0;
		} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 0
			|| force_shutdown_flags) {

			/**
			 * If abnormal/force shutdown happens in s0i3 state, EC needs to
			 * clear the flags run the shutdown sequence.
			 */
			if (system_in_s0ix)
				power_clear_s0ix_flag();

			/* Power down to next state */
			return POWER_S3S5;
		}

		if (system_in_s0ix) {
			bool has_exited_5vsb =
				!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_s0ix));
			bool has_alert = power_monitor_get_5vsb_alert();

			if (has_alert) {
				/* clear the enter 5VSB timer if over upper current */
				hook_call_deferred(&power_5vsb_enter_deferred_data, -1);
				if (!has_exited_5vsb && !power_5vsb_exit()) {
					power_clear_s0ix_flag();
					chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
					return POWER_S3S5;
				}
			} else if (!has_alert && has_exited_5vsb) {
				hook_call_deferred(&power_5vsb_enter_deferred_data, timeout_5vsb);
				CPRINTS("5V current under %dmA, don't need power supply, but waiting another %d min",
						INA236_MONITOR_5V_LOWER_CURRENT_MA,
						timeout_5vsb / MINUTE);
			}
		}

		break;

	case POWER_S3S0:

		/*
		 * TODO: distinguish S5 -> S0 and S3 -> S0, the sequences are different
		 * S5 -> S0: wait 10 - 15 ms then assert the SYSON
		 * S3 -> S0: wait 10 - 15 ms then assert the SUSP_L
		 * currently, I will follow the power on sequence to make sure DUT can
		 * power up from S5.
		 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 1);

		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);

		/* wait VS power good */
		if (power_wait_signals(IN_VS_POWER)) {
			/* something wrong, turn off power and force to g3 */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
			chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
			return POWER_S3;
		}

		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);

		/* wait VR power good */
		if (power_wait_signals(IN_VR_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			set_diagnostic(DIAGNOSTICS_HW_PGOOD_VR, 1);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
			chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
			return POWER_S3;
		}

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 1);

		lpc_s0ix_resume_restore_masks();
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);
		peripheral_power_resume();

		/* set the PD chip system power state "S0" */
		cypd_set_power_active();

		power_clear_wake_event(RTCWAKE | USBWAKE);

		return POWER_S0;

	case POWER_S0:
		/* Reset the timeout so we're power efficient entering suspend. */
		timeout_5vsb = TIMEOUT_5VSB_MIN;

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0 ||
			force_shutdown_flags) {

			/* Power down to next state */
			k_msleep(5);
			return POWER_S0S3;
		}

#ifdef CONFIG_PLATFORM_EC_POWERSEQ_S0IX
		if (check_s0ix_statsus() == CS_ENTER_S0ix)
			return POWER_S0S0ix;
#endif
		break;

#ifdef CONFIG_PLATFORM_EC_POWERSEQ_S0IX
	case POWER_S0ix:
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0 ||
			force_shutdown_flags) {
			/*
			 * If power signal lose, we need to resume to S0 and
			 * clear the all s0ix flags
			 */
			if (resume_ms_flag > 0) {
				power_clear_s0ix_flag();
				return POWER_S0ixS0;
			}

			return POWER_S0ixS3;
		}

		if (check_s0ix_statsus() == CS_EXIT_S0ix)
			return POWER_S0ixS0;

		break;

	case POWER_S0ixS3:
		/* follow power sequence Disable S3 power */

		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
		k_msleep(85);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
		peripheral_power_suspend();

		if (board_get_version() >= BOARD_VERSION_8) {
			if (power_wait_mask_signals_timeout(0, IN_VS_POWER,
				TIMEOUT_VS_POWER_TURN_OFF)) {
				/**
				 * EC needs to wait the EC_CUSTOMIZED_MEMMAP_POWER_STATE
				 * bit 7 to exit the S0ix.
				 *
				 * If the power fail during S0ix transfer to S0i3 (S3),
				 * the host can't to update the EC_CUSTOMIZED_MEMMAP_POWER_STATE.
				 * Therefore, EC should clear the flags and return power state to
				 * S0 to run the shutdown sequence.
				 */
				power_clear_s0ix_flag();
				chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
				return POWER_S0;
			}

			hook_call_deferred(&power_5vsb_enter_deferred_data, timeout_5vsb);
		}

		return POWER_S3;

	case POWER_S3S0ix:
		/* Enable power for CPU check system */
		k_msleep(10);
		if (board_get_version() >= BOARD_VERSION_8) {
			bool has_exited_5vsb =
				!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_s0ix));

			/* clear the enter 5VSB timer if resume to s0ix */
			hook_call_deferred(&power_5vsb_enter_deferred_data, -1);
			if (!has_exited_5vsb && !power_5vsb_exit()) {
				power_clear_s0ix_flag();
				chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
				/**
				 * If happens something wrong, transfer the state to POWER_S0ixS3.
				 * Eventually go all the way to S5 due to force_shutdown_flags set.
				 */
				return POWER_S0ixS3;
			}

			system_check_ssd_status();
		}
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);

		/* wait VS power good. If something wrong, turn off power and force to g3 */
		if (power_wait_signals(IN_VS_POWER)) {
			power_clear_s0ix_flag();
			chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
			/**
			 * If happens something wrong, transfer the state to POWER_S0ixS3.
			 * Eventually go all the way to S5 due to force_shutdown_flags set.
			 */
			return POWER_S0ixS3;
		}

		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);

		/* wait VR power good. If something wrong, turn off power and force to g3 */
		if (power_wait_signals(IN_VR_PGOOD)) {
			power_clear_s0ix_flag();
			chipset_force_shutdown(CHIPSET_SHUTDOWN_POWERFAIL);
			/**
			 * If happens something wrong, transfer the state to POWER_S0ixS3.
			 * Eventually go all the way to S5 due to force_shutdown_flags set.
			 */
			return POWER_S0ixS3;
		}

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 1);
		peripheral_power_resume();

		k_msleep(10);
		return POWER_S0ix;

	case POWER_S0ixS0:
		resume_ms_flag = 0;
		system_in_s0ix = 0;

		lpc_s0ix_resume_restore_masks();
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

		break;

	case POWER_S0S0ix:
		enter_ms_flag = 0;
		system_in_s0ix = 1;
		lpc_s0ix_suspend_clear_masks();
		hook_notify(HOOK_CHIPSET_SUSPEND);
		return POWER_S0ix;

		break;
#endif

	case POWER_S0S3:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
		k_msleep(85);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);

		lpc_s0ix_suspend_clear_masks();
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		peripheral_power_suspend();

		/* set the PD chip system power state "S3" */
		cypd_set_power_active();
		return POWER_S3;

	case POWER_S3S5:
		/* disable the ssd2 power when the system shutdown to S5 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 0);
		k_msleep(55);
		/* Call hooks before we remove power rails */
		power_s5_up_control(0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
		/*set_gpu_gpio(GPIO_FUNC_GPU_PWR, 0);*/
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* set the PD chip system power state "S5" */
		cypd_set_power_active();
		return POWER_S5;

	case POWER_S5G3:

		if (should_stay_powered_on())
			return POWER_S5;

		/* Don't need to keep pch power, turn off the pch power and power down to G3*/
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_1p2valw_pwren), 0);

		k_msleep(5);

		if (board_get_version() >= BOARD_VERSION_8) {
			/**
			 * ensure to turn off the en_s0ix when the system occurs
			 * the abnormal/force shutdown
			 */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_s0ix), 0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_rvsp_l), 0);
			k_msleep(5);
		}

		/* clear suspend flag when system shutdown */
		power_state_clear(EC_PS_ENTER_S0ix |
			EC_PS_RESUME_S0ix | EC_PS_RESUME_S3 | EC_PS_ENTER_S3);

		cypd_set_power_active();

		cypd_update_chips_state(CCG_STATE_NO_POWER);

		power_enable_psu(0);

		/* check the 12vb_apu signal after turn off the PSU (ps_on = low) */
		power_check_12vb_apu();

		return POWER_G3;
	default:
		break;
	}
	return state;
}

static void system_check_ssd_status(void)
{
	int ssd_power_states = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT);

	if (ssd_power_states & JSSD2_POWER_ON) {
		/* only enable the ssd2 power after the PSU is on */
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pok_l)) == 1) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 1);
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT) &= ~JSSD2_POWER_ON;
		}
	}
}
DECLARE_HOOK(HOOK_TICK, system_check_ssd_status, HOOK_PRIO_DEFAULT);

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		const struct gpio_dt_spec *prochot = GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l);

		if (board_get_version() < BOARD_VERSION_8)
			prochot = GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l_evt);
		gpio_pin_set_dt(prochot, !throttle);
	}
}

static void peripheral_device_reset(void)
{
	/**
	 * This hook is called when the system warm boots or cold boots,
	 * adding the delay time to filter the cold boot condition.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* enable ssd in warm boot */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 1);
		crec_msleep(200);
		/* do not reset the hub when the system shutdown */
		if (!chipset_in_state(CHIPSET_STATE_ON))
			return;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usbc_hub_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usba_hub_en), 0);
		crec_msleep(4);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hubc_rst_l), 0);
		power_usb_huba_reset(0);
		crec_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usbc_hub_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usba_hub_en), 1);
		crec_msleep(4);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hubc_rst_l), 1);
		power_usb_huba_reset(1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, peripheral_device_reset, HOOK_PRIO_DEFAULT);

/* EC needs to change the power signal before chipset init the interrupt */
static void power_select_power_signal(void)
{
	/**
	 * default set the dvt2 pin in the power signal devicetree, and overrite it
	 * if the mainboard is evt or dvt1
	 */
	if (board_get_version() < BOARD_VERSION_8)
		power_signal_list[X86_VR_PG].gpio = GPIO_POWER_GOOD_VR_EVT;
}
DECLARE_HOOK(HOOK_INIT, power_select_power_signal, HOOK_PRIO_INIT_CHIPSET - 1);

__override int chipset_in_low_power_mode(void)
{
	volatile uint32_t *address;
	uint32_t val;
	bool in_low_power_mode = false;

	address = NPCX_ESPI_VWEVSM_ADDR;
	/* Get Wire field */
	val = *address & 0x0F;

	if (val != 0)
		in_low_power_mode = true;

	return in_low_power_mode;
}

static int cmd_psu_s3_keep(int argc, const char **argv)
{
	char *e;

	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	if (argc == 2) {
		force_enable_psu = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	};

	CPRINTS("PSU is forced on in S3: %d", force_enable_psu);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(psu_s3_keep, cmd_psu_s3_keep,
			"[1/0]",
			"Keep the PSU on when the system is in S3.");

static enum ec_status hc_psu_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_psu_control *p = args->params;
	struct ec_response_psu_control *r = args->response;

	force_enable_psu = p->force_enable_in_standby > 0 ? true : false;

	r->force_enable_in_standby = force_enable_psu;
	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSU_CONTROL, hc_psu_control, EC_VER_MASK(0));
