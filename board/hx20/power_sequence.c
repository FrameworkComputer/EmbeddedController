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

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)


/*
 * define wake source for keep PCH power
 * BIT0 for RTCwake
 * BIT1 for USBwake
 */
#define RTCWAKE  BIT(0)
#define USBWAKE  BIT(1)

static bool want_boot_ap_at_g3;

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	/* TODO */
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

	if (version & BIT(0))
		return true;
#ifdef CONFIG_EMI_REGION1
	else if (wake_source & RTCWAKE)
		return true;
	/* when BIT1 setup, need check AC is exist*/
	else if (wake_source & USBWAKE && gpio_get_level(GPIO_AC_PRESENT) == 1)
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

static void chipset_force_g3(void)
{
	gpio_set_level(GPIO_SUSP_L, 0);
	gpio_set_level(GPIO_EC_VCCST_PG, 0);
	gpio_set_level(GPIO_VR_ON, 0);
	gpio_set_level(GPIO_PCH_PWROK, 0);
	gpio_set_level(GPIO_SYS_PWROK, 0);
	gpio_set_level(GPIO_SYSON, 0);
	/* keep pch power for wake source or vpro type */
	if (!keep_pch_power()) {
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
		gpio_set_level(GPIO_PCH_PWR_EN, 0);
		gpio_set_level(GPIO_PCH_DPWROK, 0);
		gpio_set_level(GPIO_PCH_PWRBTN_L, 0);
		gpio_set_level(GPIO_AC_PRESENT_OUT, 0);
		/*gpio_set_level(GPIO_VS_ON, 0); Todo Fix VS_ON no boot*/
	}

	f75303_set_enabled(0);

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

int board_chipset_power_on(void)
{
	/*gpio_set_level(GPIO_VS_ON, 1); Todo fix vson noboot*/

	msleep(5);

	if (power_wait_signals(IN_PGOOD_PWR_3V5V)) {
		CPRINTS("timeout waiting for PWR_3V5V_PG");
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
		CPRINTS("timeout waiting for VCCIN_AUX_VR_PG");
		chipset_force_g3();
		return false;
	}

	/* Add 10ms delay between SUSP_VR and RSMRST */
	msleep(20);

	/* Deassert RSMRST# */
	gpio_set_level(GPIO_PCH_RSMRST_L, 1);

	gpio_set_level(GPIO_AC_PRESENT_OUT, 1);

	if (want_boot_ap_at_g3) {
		CPRINTS("press power button for G3 Boot!");
		/* assert the power button to power on system */
		msleep(30);
		gpio_set_level(GPIO_PCH_PWRBTN_L, 0);
		msleep(30);
		gpio_set_level(GPIO_PCH_PWRBTN_L, 1);
		want_boot_ap_at_g3 = 0;
	}

	msleep(50);
	return true;
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
static void power_state_clear(int state)
{
	*host_get_customer_memmap(EC_EMEMAP_ER1_POWER_STATE) &= ~state;
}
#endif

static int check_s0ix_statsus(void)
{
	int power_status;
#ifdef CONFIG_EMI_REGION1
	/* check power state S0ix flags */
	if (chipset_in_state(CHIPSET_STATE_ON) || chipset_in_state(CHIPSET_STATE_STANDBY)) {
		power_status = *host_get_customer_memmap(EC_EMEMAP_ER1_POWER_STATE);
		if (power_status & EC_PS_ENTER_S0ix || power_status & EC_PS_RESUME_S0ix)
			return 1;
	}
#endif
	return 0;
}

void s0ix_status_handle(void)
{
	if (check_s0ix_statsus() && chipset_in_state(CHIPSET_STATE_ON))
		task_wake(TASK_ID_CHIPSET);
	else if (check_s0ix_statsus() && chipset_in_state(CHIPSET_STATE_STANDBY))
		task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_SECOND, s0ix_status_handle, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_POWER_S0IX */

enum power_state power_handle_state(enum power_state state)
{
	//struct batt_params batt;

	switch (state) {
	case POWER_G3:
#ifdef CONFIG_EMI_REGION1
		if (keep_pch_power()) {
			if ((power_get_signals() & IN_PCH_SLP_S5_DEASSERTED))
				return POWER_G3S5;
		}
#endif
		break;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		CPRINTS("power handle state in S0ix");
		if ((power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) == 0) {
			power_state_clear(EC_PS_RESUME_S0ix);
			return POWER_S0;
		}		
		if (check_s0ix_statsus())
			return POWER_S0ixS0;

		break;

	case POWER_S0ixS0:
		CPRINTS("power handle state in S0ix->S0");
		lpc_s0ix_resume_restore_masks();
		hook_notify(HOOK_CHIPSET_RESUME);
		power_state_clear(EC_PS_RESUME_S0ix);
		return POWER_S0;

		break;

	case POWER_S0S0ix:
		CPRINTS("power handle state in S0->S0ix");
		lpc_s0ix_suspend_clear_masks();
		hook_notify(HOOK_CHIPSET_SUSPEND);
		power_state_clear(EC_PS_ENTER_S0ix);
		return POWER_S0ix;

		break;
#endif
	case POWER_S5:
		CPRINTS("power handle state in S5");

		/* Wait for S5 exit for global reset */
		while ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
			if (task_wait_event(SECOND*9) == TASK_EVENT_TIMER) {
				CPRINTS("timeout waiting for S5 exit");
				power_button_enable_led(0);
				return POWER_S5G3; /* Power up again */
			}
		}

		return POWER_S5S3; /* Power up to next state */

		break;

	case POWER_S3:
		CPRINTS("power handle state in S3");

        if (power_get_signals() & IN_PCH_SLP_S3_DEASSERTED) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}

		break;

	case POWER_S0:
		CPRINTS("power handle state in S0");
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
		CPRINTS("power handle state in G3S5");

		if (board_chipset_power_on()) {
			cancel_board_power_off();
			return POWER_S5;
		} else {
			return POWER_G3;
		}
		break;

	case POWER_S5S3:
		CPRINTS("power handle state in S5S3");

        gpio_set_level(GPIO_SYSON, 1);

        /* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

		break;

	case POWER_S3S0:
		CPRINTS("power handle state in S3S0");

        gpio_set_level(GPIO_SUSP_L, 1);

        msleep(10);
		f75303_set_enabled(1);

        gpio_set_level(GPIO_EC_VCCST_PG, 1);

        msleep(30);

        gpio_set_level(GPIO_VR_ON, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

        if (power_wait_signals(IN_PGOOD_PWR_VR)) {
			gpio_set_level(GPIO_SUSP_L, 0);
			gpio_set_level(GPIO_EC_VCCST_PG, 0);
			gpio_set_level(GPIO_VR_ON, 0);
			f75303_set_enabled(0);
			return POWER_S3;
		}

        gpio_set_level(GPIO_PCH_PWROK, 1);

        msleep(10);

        gpio_set_level(GPIO_SYS_PWROK, 1);
#ifdef CONFIG_EMI_REGION1
		clear_rtcwake();
#endif
		power_button_enable_led(0);
        return POWER_S0;

		break;

	case POWER_S0S3:
		CPRINTS("power handle state in S0S3");
		gpio_set_level(GPIO_SUSP_L, 0);
		gpio_set_level(GPIO_PCH_PWROK, 0);
		gpio_set_level(GPIO_SYS_PWROK, 0);
		hook_notify(HOOK_CHIPSET_SUSPEND);
		f75303_set_enabled(0);
		return POWER_S3;
		break;

	case POWER_S3S5:
		CPRINTS("power handle state in S3S5");
		gpio_set_level(GPIO_SYSON, 0);
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
		return POWER_S5;
		break;

	case POWER_S5G3:
		CPRINTS("power handle state in S5G3");
		chipset_force_g3();
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
