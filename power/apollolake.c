/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Apollolake chipset power control module for Chrome EC */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "lpc.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_RSMRST_N	POWER_SIGNAL_MASK(X86_RSMRST_N)
#define IN_ALL_SYS_PG	POWER_SIGNAL_MASK(X86_ALL_SYS_PG)
#define IN_SLP_S3_N	POWER_SIGNAL_MASK(X86_SLP_S3_N)
#define IN_SLP_S4_N	POWER_SIGNAL_MASK(X86_SLP_S4_N)
#define IN_SUSPWRDNACK	POWER_SIGNAL_MASK(X86_SUSPWRDNACK)
#define IN_SUS_STAT_N	POWER_SIGNAL_MASK(X86_SUS_STAT_N)

#define IN_ALL_PM_SLP_DEASSERTED (IN_SLP_S3_N | \
				  IN_SLP_S4_N)

#define IN_PGOOD_ALL_CORE (IN_RSMRST_N)

#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

static int throttle_cpu;      /* Throttle CPU? */
static int forcing_coldreset; /* Forced coldreset in progress? */
static int power_s5_up;       /* Chipset is sequencing up or down */

__attribute__((weak)) void chipset_do_shutdown(void)
{
	/* Need to implement board specific shutdown */
}

void chipset_force_shutdown(void)
{
	if (!forcing_coldreset)
		CPRINTS("%s()", __func__);

	chipset_do_shutdown();
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);
	if (cold_reset) {
		/*
		 * Perform chipset_force_shutdown and mark forcing_coldreset.
		 * Once in S5G3 state, check forcing_coldreset to power up.
		 */
		forcing_coldreset = 1;

		chipset_force_shutdown();
	} else {
		/*
		 * Send a pulse to SOC PMU_RSTBTN_N to trigger a warm reset.
		 */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		usleep(32 * MSEC);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

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
		} else {
			/* Force all signals to their G3 states */
			chipset_force_shutdown();
		}
	}

	return POWER_G3;
}

static void handle_rsmrst_l_pgood(enum power_state state)
{
	/*
	 * Pass through asynchronously, as SOC may not react
	 * immediately to power changes.
	 */
	int in_level = gpio_get_level(GPIO_RSMRST_L_PGOOD);
	int out_level = gpio_get_level(GPIO_PCH_RSMRST_L);

	/* Nothing to do. */
	if (in_level == out_level)
		return;

	/* Only passthrough RSMRST_L de-assertion on power up */
	if (in_level && !power_s5_up)
		return;

	gpio_set_level(GPIO_PCH_RSMRST_L, in_level);

	CPRINTS("Pass through GPIO_RSMRST_L_PGOOD: %d", in_level);
}

static void handle_all_sys_pgood(enum power_state state)
{
	/*
	 * Pass through asynchronously, as SOC may not react
	 * immediately to power changes.
	 */
	int in_level = gpio_get_level(GPIO_ALL_SYS_PGOOD);
	int out_level = gpio_get_level(GPIO_PCH_SYS_PWROK);

	/* Nothing to do. */
	if (in_level == out_level)
		return;

	gpio_set_level(GPIO_PCH_SYS_PWROK, in_level);

	CPRINTS("Pass through GPIO_ALL_SYS_PGOOD: %d", in_level);
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

static enum power_state _power_handle_state(enum power_state state)
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

		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S5G3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 1) {
			/* Power up to next state */
			return POWER_S5S3;
		}
		break;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			chipset_force_shutdown();
			return POWER_S0S3;
#ifdef CONFIG_POWER_S0IX
		} else if ((power_get_host_sleep_state() ==
			    HOST_SLEEP_EVENT_S0IX_SUSPEND) &&
			   (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1)) {
			return POWER_S0S0ix;
#endif
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}

		break;

#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		/*
		 * TODO: add code for unexpected power loss
		 */
		if ((power_get_host_sleep_state() ==
		     HOST_SLEEP_EVENT_S0IX_RESUME) &&
		   (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1)) {
			return POWER_S0ixS0;
		}

		break;
#endif

	case POWER_G3S5:
		/* Platform is powering up, clear forcing_coldreset */
		forcing_coldreset = 0;

		/*
		 * Allow up to 1s for charger to be initialized, in case
		 * we're trying to boot the AP with no battery.
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

		/* Wait for RSMRST_L de-assert */
		if (power_wait_signals(IN_PGOOD_ALL_CORE)) {
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
		gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

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

		lpc_enable_wake_mask_for_lid_open();

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S0ix.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S0ix;


	case POWER_S0ixS0:
		lpc_disable_wake_mask_for_lid_open();

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
		 * while the SLP_Sx_L signals are asserted then deasserted. */
		power_s5_up = 0;
		return POWER_S5;

	case POWER_S5G3:
		chipset_force_shutdown();

		/* Power up the platform again for forced cold reset */
		if (forcing_coldreset) {
			forcing_coldreset = 0;
			return POWER_G3S5;
		}

		return POWER_G3;

	default:
		break;
	}

	return state;
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;

	/* Process ALL_SYS_PGOOD state changes. */
	handle_all_sys_pgood(state);

	new_state = _power_handle_state(state);

	/*
	 * Process RSMRST_L state changes:
	 * RSMRST_L de-assertion is passed to SoC only on G3S5 to S5 transition.
	 * RSMRST_L is also checked in some states and, if asserted, will
	 * force shutdown.
	 */
	handle_rsmrst_l_pgood(new_state);

	return new_state;
}

/**
 * chipset check if PLTRST# is valid.
 *
 * @return non-zero if PLTRST# is valid, 0 if invalid.
 */
int chipset_pltrst_is_valid(void)
{
	/*
	 * Invalid PLTRST# from SOC unless RSMRST#
	 * from PMIC through EC to soc is deasserted.
	 */
	return (gpio_get_level(GPIO_RSMRST_L_PGOOD) &&
		gpio_get_level(GPIO_PCH_RSMRST_L));
}
