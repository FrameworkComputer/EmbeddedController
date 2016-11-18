/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel X86 chipset power control module for Chrome EC */

#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "espi.h"
#include "gpio.h"
#include "hooks.h"
#include "intel_x86.h"
#include "lpc.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "wireless.h"

/* Chipset specific header files */
#ifdef CONFIG_CHIPSET_APOLLOLAKE
#include "apollolake.h"
#elif defined(CONFIG_CHIPSET_SKYLAKE)
#include "skylake.h"
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

enum sys_sleep_state {
	SYS_SLEEP_S5,
	SYS_SLEEP_S4,
	SYS_SLEEP_S3
};

int power_s5_up;       /* Chipset is sequencing up or down */

/* Get system sleep state through GPIOs or VWs */
static int chipset_get_sleep_signal(enum sys_sleep_state state)
{
#ifdef CONFIG_ESPI_VW_SIGNALS
	if (state == SYS_SLEEP_S4)
		return espi_vw_get_wire(VW_SLP_S4_L);
	else if (state == SYS_SLEEP_S3)
		return espi_vw_get_wire(VW_SLP_S3_L);
#else
	if (state == SYS_SLEEP_S4)
		return gpio_get_level(GPIO_PCH_SLP_S4_L);
	else if (state == SYS_SLEEP_S3)
		return gpio_get_level(GPIO_PCH_SLP_S3_L);
#endif

	/* We should never run here */
	ASSERT(0);
	return 0;
}

#ifdef CONFIG_BOARD_HAS_RTC_RESET
static enum power_state power_wait_s5_rtc_reset(void)
{
	static int s5_exit_tries;

	/* Wait for S5 exit and then attempt RTC reset */
	while ((power_get_signals() & IN_PCH_SLP_S4_DEASSERTED) == 0) {
		/* Handle RSMRST passthru event while waiting */
		handle_rsmrst(POWER_S5);
		if (task_wait_event(SECOND*4) == TASK_EVENT_TIMER) {
			CPRINTS("timeout waiting for S5 exit");
			chipset_force_g3();

			/* Assert RTCRST# and retry 5 times */
			board_rtc_reset();

			if (++s5_exit_tries > 4) {
				s5_exit_tries = 0;
				return POWER_G3; /* Stay off */
			}

			udelay(10 * MSEC);
			return POWER_G3S5; /* Power up again */
		}
	}

	s5_exit_tries = 0;
	return POWER_S5S3; /* Power up to next state */
}
#endif

#ifdef CONFIG_POWER_S0IX
/*
 * In AP S0 -> S3 & S0ix transitions,
 * the chipset_suspend is called.
 *
 * The chipset_in_state(CHIPSET_STATE_STANDBY | CHIPSET_STATE_ON)
 * is used to detect the S0ix transiton.
 *
 * During S0ix entry, the wake mask for lid open is enabled.
 */
static void s0ix_lpc_enable_wake_mask_for_lid_open(void)
{
	if (chipset_in_state(CHIPSET_STATE_STANDBY | CHIPSET_STATE_ON)) {
		uint32_t mask;

		mask = lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE) |
			EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN);

		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, mask);
	}
}

/*
 * In AP S0ix & S3 -> S0 transitions,
 * the chipset_resume hook is called.
 *
 * During S0ix exit, the wake mask for lid open is disabled.
 * All pending events are cleared
 */
static void s0ix_lpc_disable_wake_mask_for_lid_open(void)
{
	if (chipset_in_state(CHIPSET_STATE_STANDBY | CHIPSET_STATE_ON)) {
		uint32_t mask;

		mask = lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE) &
			~EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN);

		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, mask);

		/* clear host events */
		while (lpc_query_host_event_state() != 0)
			;
	}
}
#endif

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

enum power_state power_chipset_init(void)
{
	/*
	 * If we're switching between images without rebooting, see if the x86
	 * is already powered on; if so, leave it there instead of cycling
	 * through G3.
	 */
	if (system_jumped_to_this_image()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			/* Disable idle task deep sleep when in S0. */
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
			return POWER_S0;
		}

		/* Force all signals to their G3 states */
		chipset_force_g3();
	}

	return POWER_G3;
}

enum power_state common_intel_x86_power_handle_state(enum power_state state)
{
	int tries = 0;

	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
#ifdef CONFIG_BOARD_HAS_RTC_RESET
		/* Wait for S5 exit and attempt RTC reset it supported */
		if (power_s5_up)
			return power_wait_s5_rtc_reset();
#endif

		if (chipset_get_sleep_signal(SYS_SLEEP_S4) == 1)
			return POWER_S5S3; /* Power up to next state */
		break;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S3) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S4) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			chipset_force_shutdown();
			return POWER_S0S3;
		} else if (chipset_get_sleep_signal(SYS_SLEEP_S3) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
#ifdef CONFIG_POWER_S0IX
		} else if (power_get_host_sleep_state() ==
			    HOST_SLEEP_EVENT_S0IX_SUSPEND) {
			return POWER_S0S0ix;
#endif
		}

		break;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		/*
		 * TODO: crosbug.com/p/61645
		 * Add code to handle unexpected power loss.
		 */
		if ((power_get_host_sleep_state() ==
		     HOST_SLEEP_EVENT_S0IX_RESUME) &&
		   (chipset_get_sleep_signal(SYS_SLEEP_S3) == 1)) {
			return POWER_S0ixS0;
		}

		break;
#endif

	case POWER_G3S5:
		/*
		 * Allow charger to be initialized for upto defined tries,
		 * in case we're trying to boot the AP with no battery.
		 */
		while (charge_prevent_power_on(0) &&
		       tries++ < CHARGER_INITIALIZED_TRIES) {
			msleep(CHARGER_INITIALIZED_DELAY_MS);
		}

		/* Return to G3 if battery level is too low */
		if (charge_want_shutdown() ||
		    tries > CHARGER_INITIALIZED_TRIES) {
			CPRINTS("power-up inhibited");
			chipset_force_shutdown();
			return POWER_G3;
		}

		/* Call hooks to initialize PMIC */
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		if (power_wait_signals(CHIPSET_G3S5_POWERUP_SIGNAL)) {
			chipset_force_shutdown();
			return POWER_G3;
		}

		power_s5_up = 1;
		return POWER_S5;

	case POWER_S5S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S5G3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

#ifdef CONFIG_POWER_S0IX
		/*
		 * Clearing the S0ix flag on the path to S0
		 * to handle any reset conditions.
		 */
		power_reset_host_sleep_state(HOST_SLEEP_EVENT_S0IX_RESUME);
#endif
		return POWER_S3;

	case POWER_S3S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		}

		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);

		/* Enable wireless */
		wireless_set_state(WIRELESS_ON);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, 0);

		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);

		/* Suspend wireless */
		wireless_set_state(WIRELESS_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

#ifdef CONFIG_POWER_S0IX
		/* re-init S0ix flag */
		power_reset_host_sleep_state(HOST_SLEEP_EVENT_S0IX_RESUME);
#endif
		return POWER_S3;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0S0ix:
		/* call hooks before standby */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		s0ix_lpc_enable_wake_mask_for_lid_open();

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S0ix.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S0ix;


	case POWER_S0ixS0:
		s0ix_lpc_disable_wake_mask_for_lid_open();

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S0;
#endif

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		/* Always enter into S5 state. The S5 state is required to
		 * correctly handle global resets which have a bit of delay
		 * while the SLP_Sx_L signals are asserted then deasserted.
		 */
		power_s5_up = 0;
		return POWER_S5;

	case POWER_S5G3:
		return chipset_force_g3();

	default:
		break;
	}

	return state;
}
