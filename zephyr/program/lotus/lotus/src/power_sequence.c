/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_adc.h"
#include "board_host_command.h"
#include "chipset.h"
#include "config.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "cypress_pd_common.h"
#include "diagnostics.h"
#include "espi.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "input_module.h"
#include "lpc.h"
#include "power.h"
#include "power_sequence.h"
#include "port80.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "gpu.h"
#include "gpu_configuration.h"
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

#define IN_VR_PGOOD POWER_SIGNAL_MASK(X86_VR_PG)

static int power_s5_up;		/* Chipset is sequencing up or down */
static int ap_boot_delay = 9;	/* For global reset to wait SLP_S5 signal de-asserts */
static int s5_exit_tries;	/* For global reset to wait SLP_S5 signal de-asserts */
static int force_shoutdown_flags;
static int stress_test_enable;
static int d3cold_is_entry;	/* check the d3cold status */

/* Power Signal Input List */
const struct power_signal_info power_signal_list[] = {
	[X86_3VALW_PG] = {
		.gpio = GPIO_POWER_GOOD_3VALW,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "3VALW_PG_DEASSERTED",
	},
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_VR_PG] = {
		.gpio = GPIO_POWER_GOOD_VR,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "VR_PG_DEASSERTED",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);


static void inputdeck_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, inputdeck_resume, HOOK_PRIO_DEFAULT);


static void inputdeck_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, inputdeck_suspend, HOOK_PRIO_DEFAULT);

static void peripheral_power_startup(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, peripheral_power_startup, HOOK_PRIO_DEFAULT);

static void peripheral_power_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_invpwr), 1);
}

static void peripheral_power_shutdown(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, peripheral_power_shutdown, HOOK_PRIO_DEFAULT);

static void peripheral_power_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_invpwr), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 0);
	set_gpu_gpio(GPIO_FUNC_SSD1_POWER, 0);
	set_gpu_gpio(GPIO_FUNC_SSD2_POWER, 0);
}

static int keep_pch_power(void)
{
	int wake_source = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT);

	/* This feature only use on the ODM stress test tool */
	if (wake_source & RTCWAKE)
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

static void clear_rtcwake(void)
{
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT) &= ~BIT(0);
}

void power_state_clear(int state)
{
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_STATE) &= ~state;
}

void power_s5_up_control(int control)
{
	CPRINTS("%s power s5 up!", control ? "setup" : "clear");
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

void chipset_g3_deferred(void)
{
	set_gpu_gpios_powerstate();
}
DECLARE_DEFERRED(chipset_g3_deferred);

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

static void chipset_force_g3(void)
{
	input_modules_powerdown();
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sleep_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_b_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
	control_5valw_power(POWER_REQ_POWER_ON, 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_hub_re_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 0);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		CPRINTS("%s(%d)", __func__, reason);
		report_ap_reset(reason);
		force_shoutdown_flags = 1;
		task_wake(TASK_ID_CHIPSET);
	}
}

enum power_state power_chipset_init(void)
{
	/* If we don't need to image jump to RW, always start at G3 state */
	chipset_force_g3();
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

static int chipset_prepare_S3(uint8_t enable)
{
	if (!enable) {
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
		k_msleep(85);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
		peripheral_power_suspend();
		/* only exit epr when battery connect */
		if (battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED)
			exit_epr_mode();
	} else {
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 1);
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);

		/* wait VR power good. if something wrong, turn off power and force to g3 */
		if (power_wait_signals(IN_VR_PGOOD))
			force_shoutdown_flags = 1;

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 1);
		peripheral_power_resume();
	}

	return true;
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:

		control_5valw_power(POWER_REQ_POWER_ON, 1);

		if (power_wait_signals(X86_3VALW_PG)) {
			/* something wrong, turn off power and force to g3 */
			chipset_force_g3();
			return POWER_G3;
		}
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 1);
		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_hub_re_en), 1);
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
		power_s5_up_control(1);
		return POWER_S5;

	case POWER_S5:

		if (force_shoutdown_flags) {
			force_shoutdown_flags = 0;
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
						clear_rtcwake();
						set_diagnostic(DIAGNOSTICS_SLP_S4, 1);
						/* SLP_S5 asserted, power down to G3S5 state */
						return POWER_S5G3;
					}
				}
			}
			/* Power up to next state */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb30_hub_en), 1);
			s5_exit_tries = 0;
			return POWER_S5S3;
		}

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 1) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb30_hub_en), 1);
			s5_exit_tries = 0;
			/* Power up to next state */
			return POWER_S5S3;
		}

		break;

	case POWER_S5S3:

		/* Call hooks now that rails are up */
		hook_call_deferred(&system_hang_detect_data, 3 * SECOND);
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3:
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 1 &&
			!force_shoutdown_flags) {

			/* still in s0ix state */
			if (system_in_s0ix)
				return POWER_S3S0ix;

			/* enable the ssd2 power when the system power on from S5 */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 1);
			set_gpu_gpio(GPIO_FUNC_SSD1_POWER, 1);
			set_gpu_gpio(GPIO_FUNC_SSD2_POWER, 1);

			/* Power up to next state */
			k_msleep(10);
			return POWER_S3S0;
		} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 0
			|| force_shoutdown_flags) {

			if (system_in_s0ix) {
				resume_ms_flag = 0;
				enter_ms_flag = 0;
				system_in_s0ix = 0;
				lpc_s0ix_resume_restore_masks();
				/* Call hooks now that rails are up */
				hook_notify(HOOK_CHIPSET_RESUME);
				peripheral_power_resume();

				/* if system drop power return to S0 run sequence shutdown */
				return POWER_S0;
			}

			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb30_hub_en), 0);

			/* disable the ssd2 power when the system shutdown to S5 */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 0);
			set_gpu_gpio(GPIO_FUNC_SSD1_POWER, 0);
			set_gpu_gpio(GPIO_FUNC_SSD2_POWER, 0);
			k_msleep(55);
			/* Power down to next state */
			return POWER_S3S5;
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
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_en), 1);
		set_gpu_gpio(GPIO_FUNC_GPU_PWR, 1);

		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 1);
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);

		/* wait VR power good */
		if (power_wait_signals(IN_VR_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			set_diagnostic(DIAGNOSTICS_VCCIN_AUX_VR, 1);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
			force_shoutdown_flags = 1;
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

		clear_rtcwake();

		return POWER_S0;

	case POWER_S0:

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0 ||
			force_shoutdown_flags) {
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
			force_shoutdown_flags) {
			/*
			 * If power signal lose, we need to resume to S0 and
			 * clear the all s0ix flags
			 */
			if (resume_ms_flag > 0) {
				resume_ms_flag = 0;
				enter_ms_flag = 0;
				system_in_s0ix = 0;
				return POWER_S0ixS0;
			}

			return POWER_S0ixS3;
		}

		if (check_s0ix_statsus() == CS_EXIT_S0ix)
			return POWER_S0ixS0;

		break;

	case POWER_S0ixS3:
		/* follow power sequence Disable S3 power */
		chipset_prepare_S3(0);
		set_gpu_gpios_powerstate();
		return POWER_S3;

	case POWER_S3S0ix:
		/* Enable power for CPU check system */
		chipset_prepare_S3(1);
		set_gpu_gpios_powerstate();
		return POWER_S0ix;

	case POWER_S0ixS0:
		resume_ms_flag = 0;
		system_in_s0ix = 0;

		/* We should enter EPR mode when the system actually resume to S0 state */
		enter_epr_mode();

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
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);

		lpc_s0ix_suspend_clear_masks();
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		peripheral_power_suspend();

		/* set the PD chip system power state "S3" */
		cypd_set_power_active();
		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		power_s5_up_control(0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_en), 0);
		/*set_gpu_gpio(GPIO_FUNC_GPU_PWR, 0);*/
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		input_modules_powerdown();

		/* set the PD chip system power state "S5" */
		cypd_set_power_active();
		return POWER_S5;

	case POWER_S5G3:

		/*
		 * We need to keep pch power to wait SLP_S5 signal for the below cases:
		 *
		 * 1. Customer testing tool
		 * 2. There is a type-c USB input deck connect on the unit
		 */
		if (keep_pch_power())
			return POWER_S5;

		hook_call_deferred(&chipset_g3_deferred_data, 5 * MSEC);

		/* Don't need to keep pch power, turn off the pch power and power down to G3*/
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		control_5valw_power(POWER_REQ_POWER_ON, 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_hub_re_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 0);
		input_modules_powerdown();

		/* clear suspend flag when system shutdown */
		power_state_clear(EC_PS_ENTER_S0ix |
			EC_PS_RESUME_S0ix | EC_PS_RESUME_S3 | EC_PS_ENTER_S3);

		cypd_set_power_active();
		return POWER_G3;
	default:
		break;
	}
	return state;
}

void system_check_ssd_status(void)
{
	int ssd_power_states = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT);

	if (ssd_power_states & JSSD2_POWER_ON) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 1);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT) &= ~JSSD2_POWER_ON;
	}

	if (ssd_power_states & EXT_SSD1_POWER_ON) {
		set_gpu_gpio(GPIO_FUNC_SSD1_POWER, 1);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT) &= ~EXT_SSD1_POWER_ON;
	}

	if (ssd_power_states & EXT_SSD2_POWER_ON) {
		set_gpu_gpio(GPIO_FUNC_SSD2_POWER, 1);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT) &= ~EXT_SSD2_POWER_ON;
	}
}
DECLARE_HOOK(HOOK_TICK, system_check_ssd_status, HOOK_PRIO_DEFAULT);

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), !throttle);
}

static void usb30_hub_reset(void)
{
	/**
	 * This hook is called when the system warm boots or cold boots,
	 * adding the delay time to filter the cold boot condition.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd2_pwr_en), 1);
		set_gpu_gpio(GPIO_FUNC_SSD1_POWER, 1);
		set_gpu_gpio(GPIO_FUNC_SSD2_POWER, 1);
		usleep(200 * MSEC);
		/* do not reset the hub when the system shutdown */
		if (!chipset_in_state(CHIPSET_STATE_ON))
			return;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb30_hub_en), 0);
		usleep(10 * MSEC);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb30_hub_en), 1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, usb30_hub_reset, HOOK_PRIO_DEFAULT);

static enum ec_status set_ap_reboot_delay(struct host_cmd_handler_args *args)
{
	const struct ec_response_ap_reboot_delay *p = args->params;

	stress_test_enable = 1;
	/* don't let AP send zero it will stuck power sequence at S5 */
	if (p->delay < 181 && p->delay)
		ap_boot_delay = p->delay;
	else
		return EC_ERROR_INVAL;


	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_AP_REBOOT_DELAY, set_ap_reboot_delay,
			EC_VER_MASK(0));
