/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include "keyboard_8042_sharedlib.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "power.h"
#include "port80.h"
#include "power_sequence.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

#define IN_VR_PGOOD POWER_SIGNAL_MASK(X86_VR_PG)

static int power_s5_up;		/* Chipset is sequencing up or down */
static int ap_boot_delay = 9;	/* For global reset to wait SLP_S5 signal de-asserts */
static int s5_exit_tries;	/* For global reset to wait SLP_S5 signal de-asserts */
static int force_g3_flags;	/* Chipset force to g3 immediately when chipset force shutdown */
static int stress_test_enable;
static int me_change;

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
}

void update_me_change(int change)
{
	me_change = change;
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

/**
 * AMD recommended EC needs to check whether the system hangs or not,
 * If EC detect the system hangs, force reset the system then reboot again.
 */
#define VW_NO_READY 0
void system_hang_detect(void)
{
	static int hang_retry_count;
	int virtual_wire_ready = get_espi_virtual_wire_channel_status();

	if (virtual_wire_ready == VW_NO_READY && hang_retry_count < 4) {
		hang_retry_count++;
		board_reboot_ap_on_g3();
		chipset_force_shutdown(CHIPSET_RESET_HANG_REBOOT);
	}
}
DECLARE_DEFERRED(system_hang_detect);

static void chipset_force_g3(void)
{
	hook_call_deferred(&system_hang_detect_data, -1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 0);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		report_ap_reset(reason);
		force_g3_flags = 1;
		chipset_force_g3();
	}
}

enum power_state power_chipset_init(void)
{
	/* If we don't need to image jump to RW, always start at G3 state */
	chipset_force_g3();
	return POWER_G3;
}



static int chipset_prepare_S3(uint8_t enable)
{
	if (!enable) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
		k_msleep(85);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pwr_en), 0);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 1);
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pwr_en), 1);

		/* wait VR power good */
		if (power_wait_signals(IN_VR_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			power_chipset_init();
		}

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 1);
	}

	return true;
}

/*
 * Issue : 866a60d67
 * for this issue, if into S3 and resume back by keyboard, system would not wake
 * because scan code was send before espi_rst, at the moment system would not
 * take any scan code, co-work with BIOS when Keyboard event send,
 * do this workaround to send a scan code again
 */
static void key_stuck_wa(void);
DECLARE_DEFERRED(key_stuck_wa);
static void key_stuck_wa(void)
{
	int wa_bit = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_DISPLAY_ON);

	if (wa_bit) {
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_DISPLAY_ON) = 0;
		hook_call_deferred(&key_stuck_wa_data, -1);
		simulate_keyboard(SCANCODE_UNASSIGNED, 1);
		simulate_keyboard(SCANCODE_UNASSIGNED, 0);
	} else
		hook_call_deferred(&key_stuck_wa_data, 10 * MSEC);

}

/*
 * Issue : 866ajywad
 * for this issue, if into S3 and resume back by power button,
 * system would not take Keybuffer(DBBOUT) after EC send IRQ1,
 * add this workaround EC manually send Keybuffer  and clearn OBF flag,
 * it will regenerate a IRQ signal to notice system.
 */
static void key_stuck_ptn(void)
{
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_DISPLAY_ON) = 0;
	simulate_keyboard(SCANCODE_UNASSIGNED, 1);
	simulate_keyboard(SCANCODE_UNASSIGNED, 0);
	keyboard_clear_buffer();
	/* stop key_stuck_wa if system already wake */
	hook_call_deferred(&key_stuck_wa_data, -1);
}
DECLARE_DEFERRED(key_stuck_ptn);

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:
		if (power_wait_signals(X86_3VALW_PG)) {
			/* something wrong, turn off power and force to g3 */
			chipset_force_g3();
			return POWER_G3;
		}
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 1);
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
		power_s5_up_control(1);
		return POWER_S5;

	case POWER_S5:

		if (force_g3_flags) {
			force_g3_flags = 0;
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
						set_diagnostic(DIAGNOSTICS_SLP_S4, 1);
						/* SLP_S5 asserted, power down to G3S5 state */
						return POWER_S5G3;
					}
				}
			}
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

		/* Call hooks now that rails are up */
		hook_call_deferred(&system_hang_detect_data, 5 * SECOND);
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3:
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 1) {

			/* still in s0ix state */
			if (system_in_s0ix)
				return POWER_S3S0ix;

			k_msleep(10);
			return POWER_S3S0;
		} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)) == 0) {

			if (system_in_s0ix) {
				resume_ms_flag = 0;
				enter_ms_flag = 0;
				system_in_s0ix = 0;
				lpc_s0ix_resume_restore_masks();
				/* Call hooks now that rails are up */
				hook_notify(HOOK_CHIPSET_RESUME);

				/* if system drop power return to S0 run sequence shutdown */
				return POWER_S0;
			}
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
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 1);
		k_msleep(20);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pwr_en), 1);

		/* wait VR power good */
		if (power_wait_signals(IN_VR_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			set_diagnostic(DIAGNOSTICS_VCCIN_AUX_VR, 1);
			chipset_force_g3();
			return POWER_G3;
		}

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 1);

		lpc_s0ix_resume_restore_masks();
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/* set the PD chip system power state "S0" */
		cypd_set_power_active();

		clear_rtcwake();

		return POWER_S0;

	case POWER_S0:

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0) {
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
		CPRINTS("PH S0ix");
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0) {
			/*
			 * If power signal lose, we need to resume to S0 and
			 * clear the resume ms flag
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
		CPRINTS("PH S0ixS3");
		/* make sure next time wake flag not error trigger */
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_DISPLAY_ON) = 0;
		hook_call_deferred(&key_stuck_wa_data, -1);
		/* follow power sequence Disable S3 power */
		chipset_prepare_S3(0);
		CPRINTS("PH S0ixS0->S3");
		return POWER_S3;

	case POWER_S3S0ix:
		CPRINTS("PH S3->S0ix");
		hook_call_deferred(&key_stuck_wa_data, 1);
		/* Enable power for CPU check system */
		chipset_prepare_S3(1);
		CPRINTS("PH S3S0ix->S0ix");
		return POWER_S0ix;

	case POWER_S0ixS0:
		CPRINTS("PH S0ixS0");
		resume_ms_flag = 0;
		system_in_s0ix = 0;
		hook_call_deferred(&key_stuck_ptn_data, 100 * MSEC);
		lpc_s0ix_resume_restore_masks();
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);
		CPRINTS("PH S0ixS0->S0");
		return POWER_S0;

		break;

	case POWER_S0S0ix:
		enter_ms_flag = 0;
		system_in_s0ix = 1;
		CPRINTS("PH S0->S0ix");
		lpc_s0ix_suspend_clear_masks();
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		CPRINTS("PH S0S0ix->S0ix");
		return POWER_S0ix;

		break;
#endif

	case POWER_S0S3:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrgd_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
		k_msleep(85);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75vs_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_pwr_en), 0);

		lpc_s0ix_suspend_clear_masks();
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* set the PD chip system power state "S3" */
		cypd_set_power_active();
		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		power_s5_up_control(0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

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

		/* Don't need to keep pch power, turn off the pch power and power down to G3*/
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_apu_aud_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_0p75_1p8valw_pwren), 0);

		cypd_set_power_active();


		return POWER_G3;
	default:
		break;
	}
	return state;
}

static void peripheral_power_startup(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, peripheral_power_startup, HOOK_PRIO_DEFAULT);

static void peripheral_power_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, peripheral_power_resume, HOOK_PRIO_DEFAULT);

static void peripheral_power_shutdown(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wl_rst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, peripheral_power_shutdown, HOOK_PRIO_DEFAULT);

static void peripheral_power_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, peripheral_power_suspend, HOOK_PRIO_DEFAULT);

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), !throttle);
}

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

static enum ec_status me_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_me_control *p = args->params;

	power_s5_up_control(0); /* power down pch to process ME change */

	/* CPU change ME mode based on ME_EN while RSMRST rising.
	 * So, when we received ME control command, we need to change ME_EN when power on.
	 * ME_EN low = lock.
	 */
	if (p->me_mode & ME_UNLOCK)
		update_me_change(ME_UNLOCK);
	else
		update_me_change(ME_LOCK);

	CPRINTS("Receive ME %s\n", (p->me_mode & ME_UNLOCK) == ME_UNLOCK ? "unlock" : "lock");
	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_ME_CONTROL, me_control,
			EC_VER_MASK(0));
