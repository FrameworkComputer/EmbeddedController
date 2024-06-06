/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_host_command.h"
#include "board_adc.h"
#include "chipset.h"
#include "config.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "cypress_pd_common.h"
#include "diagnostics.h"
#include "espi.h"
#include "extpower.h"
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
static bool module_pwr_control;

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
	[X86_PRIM_PWR] = {
		.gpio = GPIO_POWER_GOOD_PRIM_PWR,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "PRIM_PWR_DEASSERTED",
	},
	[X86_SLP_S4_N] = {
		.gpio = GPIO_PCH_SLP_S4_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S4_DEASSERTED",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

static int keep_pch_power(void)
{
	int wake_source = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_WAKE_EVENT);
	uint8_t vpro_change;

	system_get_bbram(SYSTEM_BBRAM_IDX_VPRO_STATUS, &vpro_change);

	/* This feature only use on the ODM stress test tool */
	if (wake_source & RTCWAKE)
		return true;
	else if (extpower_is_present() && vpro_change)
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

static void chipset_force_g3(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ac_present), 0);
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

static void control_module_power(void)
{
	static int pre_touchpad;
#ifdef CONFIG_PLATFORM_IGNORED_TOUCHPAD_ID
	int touchpad = BOARD_VERSION_10;
#else
	int touchpad = get_hardware_id(ADC_TOUCHPAD_ID);
#endif /* CONFIG_PLATFORM_IGNORED_TOUCHPAD_ID */

	if (!module_pwr_control)
		return;

	if (pre_touchpad != touchpad) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on),
			(touchpad >= BOARD_VERSION_1 && touchpad <= BOARD_VERSION_13) ? 1 : 0);

		pre_touchpad = touchpad;
	}
}
DECLARE_HOOK(HOOK_TICK, control_module_power, HOOK_PRIO_DEFAULT);

static void module_pwr_control_enable(bool state)
{
	module_pwr_control = state;
	if (module_pwr_control)
		control_module_power();
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_module_pwr_on), 0);
}

void me_gpio_change(uint32_t flags)
{
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_me_en), flags);
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 1);

		k_msleep(10);
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_prim_pwr_ok)) == 0) {
			set_diagnostic(DIAGNOSTICS_VCCIN_AUX_VR, true);
			return POWER_G3;
		}

		k_msleep(10);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 1);
		me_gpio_change(me_change & ME_UNLOCK ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW);

		if (extpower_is_present())
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ac_present), 1);

		/* Customizes power button out signal without PB task for powering on. */
		k_msleep(90);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		k_msleep(50);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 1);

		power_s5_up_control(1);
		return POWER_S5;

	case POWER_S5:

		if (force_g3_flags) {
			force_g3_flags = 0;
			return POWER_S5G3;
		}

		if (power_s5_up || stress_test_enable) {
			while (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s4_l)) == 0) {
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
			s5_exit_tries = 0;
			return POWER_S5S3;
		}

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s4_l)) == 1) {
			s5_exit_tries = 0;
			/* Power up to next state */
			return POWER_S5S3;
		}

		break;

	case POWER_S5S3:

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 1);
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3:
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 1)
			return POWER_S3S0;
		else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s4_l)) == 0) {
			/* de-asserted the syson < 0.2 ms */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_syson), 0);
			return POWER_S3S5;
		}

		break;

	case POWER_S3S0:

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 1);
		k_msleep(35);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 1);

		/* wait VR power good */
		if (power_wait_signals(IN_VR_PGOOD)) {
			/* something wrong, turn off power and force to g3 */
			set_diagnostic(DIAGNOSTICS_HW_PGOOD_VR, 1);
			chipset_force_g3();
			return POWER_G3;
		}

		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwrok_ls), 1);
		k_msleep(10);
		me_gpio_change(GPIO_INPUT);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrok_ls), 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/* set the PD chip system power state "S0" */
		cypd_set_power_active();

		clear_rtcwake();
		module_pwr_control_enable(true);

		return POWER_S0;

	case POWER_S0:

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) == 0) {
			/* Power down to next state */
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_vr_on), 0);
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
			enter_ms_flag = 0;
			return POWER_S0ixS0;
		}

		if (check_s0ix_statsus() == CS_EXIT_S0ix)
			return POWER_S0ixS0;

		break;

	case POWER_S0ixS0:
		CPRINTS("PH S0ixS0");
		resume_ms_flag = 0;
		system_in_s0ix = 0;
		lpc_s0ix_resume_restore_masks();
		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);
		return POWER_S0;

	case POWER_S0S0ix:
		enter_ms_flag = 0;
		system_in_s0ix = 1;
		CPRINTS("PH S0->S0ix");
		lpc_s0ix_suspend_clear_masks();
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		return POWER_S0ix;
#endif

	case POWER_S0S3:
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_susp_l), 0);
		me_gpio_change(GPIO_OUTPUT_LOW);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwrok_ls), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_pwrok_ls), 0);

		lpc_s0ix_suspend_clear_masks();
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* set the PD chip system power state "S3" */
		cypd_set_power_active();
		return POWER_S3;

	case POWER_S3S5:
		power_s5_up_control(0);

		/* Call hooks before we remove power rails */
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
		k_msleep(5);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pbtn_out), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_wlan_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pch_pwr_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ac_present), 0);
		k_msleep(1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l), 0);

		cypd_set_power_active();
		return POWER_G3;
	default:
		break;
	}
	return state;
}

static void peripheral_interrupt_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_enkbl));
}
DECLARE_HOOK(HOOK_INIT, peripheral_interrupt_init, HOOK_PRIO_DEFAULT);

static void peripheral_power_startup(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_rt_gpio6_ctrl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, peripheral_power_startup, HOOK_PRIO_DEFAULT);

static void peripheral_power_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, peripheral_power_resume, HOOK_PRIO_DEFAULT);

static void peripheral_power_shutdown(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_h_prochot_l), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_rt_gpio6_ctrl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_en), 0);
	module_pwr_control_enable(false);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, peripheral_power_shutdown, HOOK_PRIO_DEFAULT);

static void peripheral_power_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_mute_l), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, peripheral_power_suspend, HOOK_PRIO_DEFAULT);

/* according to Panel team suggest, delay 60ms to meet spec */
static void bkoff_on_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 1);
}
DECLARE_DEFERRED(bkoff_on_deferred);

void soc_signal_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_enbkl_ls)))
		hook_call_deferred(&bkoff_on_deferred_data, 60 * MSEC);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sm_panel_bken_ec), 0);
}

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

static enum ec_status vpro_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_vpro_control *p = args->params;
	uint8_t status;

	if (p->vpro_mode & VPRO_ON)
		status = VPRO_ON;
	else
		status = VPRO_OFF;

	system_set_bbram(SYSTEM_BBRAM_IDX_VPRO_STATUS, status);
	CPRINTS("Receive Vpro %s\n", (p->vpro_mode & VPRO_ON) == VPRO_ON ? "on" : "off");
	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_VPRO_CONTROL, vpro_control,
			EC_VER_MASK(0));
