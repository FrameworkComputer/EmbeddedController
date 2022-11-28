/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "battery.h"
#include "board.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "i2c.h"
#include "lb_common.h"
#include "lpc.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command_customization.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "wireless.h"
#include "driver/temp_sensor/f75303.h"
#include "driver/temp_sensor/f75397.h"
#include "diagnostics.h"
#include "cypress5525.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static int forcing_shutdown;  /* Forced shutdown in progress? */
static int custom_forcing_shutdown;

/*
 * define wake source for keep PCH power
 * BIT0 for RTCwake
 * BIT1 for USBwake
 */
#define RTCWAKE  BIT(0)
#define USBWAKE  BIT(1)

static bool want_boot_ap_at_g3;
static int ap_boot_delay = 9;  /* set 9 second for global reset wait time  */
static int me_change;
static int power_s5_up;
static int s5_exit_tries;
static int rtc_reset_tries;
static int stress_test_enable;


static void chipset_force_g3(void);


static void intel_x86_rtc_reset(void)
{
	CPRINTS("Asserting RTCRST# to PCH");
	gpio_set_level(GPIO_EC_RTCRST, 1);
	udelay(100);
	gpio_set_level(GPIO_EC_RTCRST, 0);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);

	/*
	 * Force off. Sending a reset command to the PMIC will power off
	 * the EC, so simulate a long power button press instead. This
	 * condition will reset once the state machine transitions to G3.
	 * Consider reducing the latency here by changing the power off
	 * hold time on the PMIC.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		report_ap_reset(reason);
		forcing_shutdown = 1;
		custom_forcing_shutdown = 1;
		chipset_force_g3();
		/* we need to clear the wake source to avoid the wrong power state */
		*host_get_customer_memmap(0x02) = 0x00;
		system_set_bbram(SYSTEM_BBRAM_IDX_VPRO_STATUS, 0);
	}
}

void chipset_handle_espi_reset_assert(void)
{
	/*
	 * If eSPI_Reset# pin is asserted without SLP_SUS# being asserted, then
	 * it means that there is an unexpected power loss (global reset
	 * event). In this case, check if shutdown was being forced by pressing
	 * power button. If yes, release power button.
	 */
	if ((power_get_signals() & IN_PCH_SLP_SUS_DEASSERTED) &&
		forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;

	}
}

/*
 * check EMI region 1 0x02 to control pch power
 * if bit set up will keep pch power for RTCwake
 * when the unit is vpro type also will keep pch
 *
 * @return false to disable pch power, true will keep pch
 */

int keep_pch_power(void)
{
	int version = board_get_version();
	int wake_source = *host_get_customer_memmap(0x02);
	uint8_t vpro_change;

	system_get_bbram(SYSTEM_BBRAM_IDX_VPRO_STATUS, &vpro_change);

	if (custom_forcing_shutdown && power_get_state() == POWER_S5G3) {
		custom_forcing_shutdown = 0;
		return false;
	}
	else if (version & BIT(0) && extpower_is_present() && vpro_change)
		return true;
#ifdef CONFIG_EMI_REGION1
	else if (wake_source & RTCWAKE)
		return true;
	/* when BIT1 setup, need check AC is exist*/
	else if (wake_source & USBWAKE && extpower_is_present())
		return true;
#endif
	else
		return false;
}

#ifdef CONFIG_EMI_REGION1
static void clear_rtcwake(void)
{
	*host_get_customer_memmap(0x02) &= ~BIT(0);
}
#endif

void s5_power_up_control(int control)
{
	CPRINTS("%s s5 power up!", control ? "setup" : "clear");
	power_s5_up = control;
}

static void chipset_force_g3(void)
{
	gpio_set_level(GPIO_VR_ON, 0);
	gpio_set_level(GPIO_EC_VCCST_PG, 0);
	gpio_set_level(GPIO_SUSP_L, 0);
	gpio_set_level(GPIO_PCH_PWROK, 0);
	gpio_set_level(GPIO_SYS_PWROK, 0);
	gpio_set_level(GPIO_SYSON, 0);
	/* keep pch power for wake source or vpro type */
	if (!keep_pch_power() || me_change) {

		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
		gpio_set_level(GPIO_PCH_PWR_EN, 0);
		gpio_set_level(GPIO_PCH_DPWROK, 0);
		gpio_set_level(GPIO_PCH_PWRBTN_L, 0);
		gpio_set_level(GPIO_AC_PRESENT_OUT, 0);
		/*gpio_set_level(GPIO_VS_ON, 0); Todo Fix VS_ON no boot*/
	}

	f75303_set_enabled(0);
	f75397_set_enabled(0);

}

void chipset_reset(enum chipset_reset_reason reason)
{
	/* TODO */
}

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_set_level(GPIO_EC_PROCHOT_L, !throttle);
}

static int system_prevent_power_on_flag;
void update_prevent_power_on_flag(int status)
{
	system_prevent_power_on_flag = status;
}

int board_chipset_power_on(void)
{
	/*gpio_set_level(GPIO_VS_ON, 1); Todo fix vson noboot*/

	msleep(5);

	if (power_wait_signals(IN_PGOOD_PWR_3V5V)) {
		CPRINTS("PH Timeout PWR_3V5V_PG");
		set_hw_diagnostic(DIAGNOSTICS_HW_PGOOD_3V5V, 1);
		chipset_force_g3();
		return false;
	}

	gpio_set_level(GPIO_PCH_PWR_EN, 1);

	msleep(10);

	gpio_set_level(GPIO_PCH_PWRBTN_L, 1);

	msleep(30);

	gpio_set_level(GPIO_PCH_DPWROK, 1);

	msleep(5);

	if (power_wait_signals(IN_PGOOD_VCCIN_AUX_VR)) {
		CPRINTS("PH Timeout VCCIN_AUX_VR_PG");
		set_hw_diagnostic(DIAGNOSTICS_VCCIN_AUX_VR, 1);
		chipset_force_g3();
		return false;
	}

	me_gpio_change(me_change & ME_UNLOCK ? GPIO_OUT_HIGH : GPIO_OUT_LOW);

	/* Add 10ms delay between SUSP_VR and RSMRST */
	msleep(20);

	/* Deassert RSMRST# */
	gpio_set_level(GPIO_PCH_RSMRST_L, 1);

	if (extpower_is_present())
		gpio_set_level(GPIO_AC_PRESENT_OUT, 1);

	if (want_boot_ap_at_g3 || rtc_reset_tries) {
		CPRINTS("Assert the power button signal to power on system!");
		/* assert the power button to power on system */
		msleep(30);
		gpio_set_level(GPIO_PCH_PWRBTN_L, 0);
		msleep(30);
		gpio_set_level(GPIO_PCH_PWRBTN_L, 1);
		want_boot_ap_at_g3 = 0;
	}

	msleep(50);

	/**
	 *  if EC doesn't assert the power button signal vai prevent power on,
	 *  the power state should return to G3 state
	 */
	CPRINTS("Power on flag: %d", system_prevent_power_on_flag);
	power_button_enable_led(!system_prevent_power_on_flag);
	return !system_prevent_power_on_flag;
}

enum power_state power_chipset_init(void)
{
	chipset_force_g3();
	return POWER_G3;
}

#ifdef CONFIG_POWER_S0IX
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

#ifdef CONFIG_EMI_REGION1
void power_state_clear(int state)
{
	*host_get_customer_memmap(EC_EMEMAP_ER1_POWER_STATE) &= ~state;
}
#endif

static int enter_ms_flag;
static int resume_ms_flag;

static int check_s0ix_statsus(void)
{
	int power_status;
	int clear_flag;
#ifdef CONFIG_EMI_REGION1
	/* check power state S0ix flags */
	if (chipset_in_state(CHIPSET_STATE_ON) || chipset_in_state(CHIPSET_STATE_STANDBY)) {
		power_status = *host_get_customer_memmap(EC_EMEMAP_ER1_POWER_STATE);

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

		if (enter_ms_flag || resume_ms_flag)
			return 1;
	}
#endif
	return 0;
}

void s0ix_status_handle(void)
{
	int s0ix_state_change;

	s0ix_state_change = check_s0ix_statsus();

	if (s0ix_state_change && chipset_in_state(CHIPSET_STATE_ON))
		task_wake(TASK_ID_CHIPSET);
	else if (s0ix_state_change && chipset_in_state(CHIPSET_STATE_STANDBY))
		task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_TICK, s0ix_status_handle, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_POWER_S0IX */

enum power_state power_handle_state(enum power_state state)
{
	//struct batt_params batt;

	switch (state) {
	case POWER_G3:
		break;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		CPRINTS("PH S0ix");
		if ((power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) == 0) {
			/* 
			 * If power signal lose, we need to resume to S0 and
			 * clear the resume ms flag 
			 */
			if (resume_ms_flag > 0)
				resume_ms_flag--;
			return POWER_S0;
		}		
		if (check_s0ix_statsus())
			return POWER_S0ixS0;

		break;

	case POWER_S0ixS0:
		CPRINTS("PH S0ixS0");
		lpc_s0ix_resume_restore_masks();
		hook_notify(HOOK_CHIPSET_RESUME);
		if (resume_ms_flag > 0)
			resume_ms_flag--;
		CPRINTS("PH S0ixS0->S0");
		cypd_set_power_active(POWER_S0);
		return POWER_S0;

		break;

	case POWER_S0S0ix:
		CPRINTS("PH S0->S0ix");
		lpc_s0ix_suspend_clear_masks();
		hook_notify(HOOK_CHIPSET_SUSPEND);
		if (enter_ms_flag > 0)
			enter_ms_flag--;
		CPRINTS("PH S0S0ix->S0ix");
		cypd_set_power_active(POWER_S3);
		return POWER_S0ix;

		break;
#endif
	case POWER_S5:
		CPRINTS("PH S5");

		if (custom_forcing_shutdown)
			/* force shutdown process shouldn't keep PCH power */
			return POWER_S5G3;

		/**
		 * power up process: wait SLP_S4 signal, if not detect the SLP_S4 in 9s,
		 * EC will do the rtc reset 5 times.
		 *
		 * stress test process: ap_boot_delay will be change via host command, wait
		 * pch to auto power up.
		 */
		if (power_s5_up || stress_test_enable) {
			while ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {

				if (task_wait_event(SECOND) == TASK_EVENT_TIMER) {

					if (++s5_exit_tries > ap_boot_delay) {
						CPRINTS("timeout waiting for S5");
						power_button_enable_led(0);
						s5_exit_tries = 0;
						ap_boot_delay = 9;
						set_hw_diagnostic(DIAGNOSTICS_SLP_S4, 1);

						if (!stress_test_enable && (++rtc_reset_tries < 6)) {
							/**
							 * When we can't detect the SLP_S4 signal, we need to remove
							 * all power rail, and doing the rtc reset to power up again.
							 */
							chipset_force_g3();
							intel_x86_rtc_reset();
							udelay(10 * MSEC);
							return POWER_G3S5;
						}

						/* clear the status */
						stress_test_enable = 0;

						/* Still can't power on, go into G3S5 state */
						if (rtc_reset_tries >= 6) {
							rtc_reset_tries = 0;
							return POWER_S5G3;
						}
					}

					/* power up again */
					return POWER_G3S5;
				}
			}

			 /* Power up to next state */
			s5_exit_tries = 0;
			rtc_reset_tries = 0;
			return POWER_S5S3;
		}

		/* shutdown process */
		s5_exit_tries = 0;
		if ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == IN_PCH_SLP_S4_DEASSERTED)
			return POWER_S5S3;

		break;

	case POWER_S3:
		CPRINTS("PH S3");

        if (power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}

		break;

	case POWER_S0:
		CPRINTS("PH S0");
		if ((power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) == 0) {
			/* Power down to next state */
			gpio_set_level(GPIO_EC_VCCST_PG, 0);
			gpio_set_level(GPIO_VR_ON, 0);
			return POWER_S0S3;
		}
#ifdef CONFIG_POWER_S0IX
		if (check_s0ix_statsus())
			return POWER_S0S0ix;
#endif
		break;

	case POWER_G3S5:

		/* wait S5 signal, so return POWER S5 state */
		if (s5_exit_tries != 0)
			return POWER_S5;

		s5_power_up_control(1);

		if (board_chipset_power_on()) {
			cancel_board_power_off();
			CPRINTS("PH G3S5->S5");
			
			return POWER_S5;
		} else {
			system_prevent_power_on_flag = 0;
			return POWER_G3;
		}
		break;

	case POWER_S5S3:
		CPRINTS("PH S5S3");

        gpio_set_level(GPIO_SYSON, 1);

        /* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		CPRINTS("PH S5S3->S3");
		return POWER_S3;

		break;

	case POWER_S3S0:
		CPRINTS("PH S3S0");
        gpio_set_level(GPIO_SUSP_L, 1);

        msleep(10);
		f75303_set_enabled(1);
		f75397_set_enabled(1);

        gpio_set_level(GPIO_EC_VCCST_PG, 1);

        msleep(30);

        gpio_set_level(GPIO_VR_ON, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

        if (power_wait_signals(IN_PGOOD_PWR_VR)) {
			set_hw_diagnostic(DIAGNOSTICS_HW_PGOOD_VR, true);

			gpio_set_level(GPIO_SUSP_L, 0);
			gpio_set_level(GPIO_EC_VCCST_PG, 0);
			gpio_set_level(GPIO_VR_ON, 0);
			f75303_set_enabled(0);
			f75397_set_enabled(0);
			return POWER_S3;
		}

        gpio_set_level(GPIO_PCH_PWROK, 1);

        msleep(10);

        gpio_set_level(GPIO_SYS_PWROK, 1);
#ifdef CONFIG_EMI_REGION1
		clear_rtcwake();
#endif
		power_button_enable_led(0);

		me_gpio_change(GPIO_FLAG_NONE);

		cypd_set_power_active(POWER_S0);
		CPRINTS("PH S3S0->S0");
        return POWER_S0;

		break;

	case POWER_S0S3:
		CPRINTS("PH S0S3");
		gpio_set_level(GPIO_SUSP_L, 0);
		gpio_set_level(GPIO_PCH_PWROK, 0);
		gpio_set_level(GPIO_SYS_PWROK, 0);
		hook_notify(HOOK_CHIPSET_SUSPEND);
		me_gpio_change(GPIO_OUT_LOW);
		f75303_set_enabled(0);
		f75397_set_enabled(0);
		return POWER_S3;
		break;

	case POWER_S3S5:
		CPRINTS("PH S3S5");
		gpio_set_level(GPIO_SYSON, 0);
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		cypd_set_power_active(POWER_S5);
		set_non_acpi_mode(0);
		power_s5_up = 0;
		return POWER_S5;
		break;

	case POWER_S5G3:
		CPRINTS("PH S5G3");
		/* if we need to keep pch power, return to S5 state */

#ifdef CONFIG_EMI_REGION1
		if (keep_pch_power())
			return POWER_S5;
#endif
		chipset_force_g3();
		/* clear suspend flag when system shutdown */
		power_state_clear(EC_PS_ENTER_S0ix |
			EC_PS_RESUME_S0ix | EC_PS_RESUME_S3 | EC_PS_ENTER_S3);
		if (!extpower_is_present()) {
			board_power_off();
		}

		return POWER_G3;
		break;
	}

	return state;
}

void boot_ap_on_g3(void)
{
	CPRINTS("Need to boot ap on g3");
	want_boot_ap_at_g3 = 1;
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

void me_gpio_change(uint32_t flags)
{
	gpio_set_flags(GPIO_ME_EN, flags);
}

void update_me_change(int change)
{
	me_change = change;
}

static enum ec_status me_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_me_control *p = args->params;

	s5_power_up_control(0); /* power down pch to process ME change */

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
	uint8_t vpro_status;

	if (p->vpro_mode & VPRO_ON)
		vpro_status = VPRO_ON;
	else
		vpro_status = VPRO_OFF;

	system_set_bbram(SYSTEM_BBRAM_IDX_VPRO_STATUS, vpro_status);
	CPRINTS("Receive Vpro %s\n", (p->vpro_mode & VPRO_ON) == VPRO_ON ? "on" : "off");
	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_VPRO_CONTROL, vpro_control,
			EC_VER_MASK(0));
