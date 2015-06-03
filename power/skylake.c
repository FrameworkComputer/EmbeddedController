/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skylake IMVP8 / ROP PMIC chipset power control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PCH_SLP_S0_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S0_DEASSERTED)
#define IN_PCH_SLP_S3_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S4_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)

#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3_DEASSERTED | \
				  IN_PCH_SLP_S4_DEASSERTED | \
				  IN_PCH_SLP_SUS_DEASSERTED)

/*
 * DPWROK is NC / stuffing option on initial boards.
 * TODO(shawnn): Figure out proper control signals.
 */
#define IN_PGOOD_ALL_CORE 0

#define IN_ALL_S0 (IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

static int throttle_cpu;      /* Throttle CPU? */
static int forcing_shutdown;  /* Forced shutdown in progress? */

void chipset_force_shutdown(void)
{
	CPRINTS("%s()", __func__);

	/*
	 * Force off. Sending a reset command to the PMIC will power off
	 * the EC, so simulate a long power button press instead. This
	 * condition will reset once the state machine transitions to G3.
	 * Consider reducing the latency here by changing the power off
	 * hold time on the PMIC.
	 */
	forcing_shutdown = 1;
	power_button_pch_press();
}

void chipset_force_g3(void)
{
	CPRINTS("Forcing G3");
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);

	if (cold_reset) {
		if (gpio_get_level(GPIO_SYS_RESET_L) == 0)
			return;
		gpio_set_level(GPIO_SYS_RESET_L, 0);
		/* Debounce time for SYS_RESET_L is 16 ms */
		udelay(20 * MSEC);
		gpio_set_level(GPIO_SYS_RESET_L, 1);
	} else {
		/*
		 * Send a RCIN_PCH_RCIN_L
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

void chipset_thottle_cpu(int throttle)
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
			chipset_force_g3();
		}
	}

	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	gpio_set_level(GPIO_PCH_RSMRST_L, gpio_get_level(GPIO_RSMRST_L_PGOOD));

	switch (state) {
	case POWER_G3:
		if (forcing_shutdown) {
			power_button_pch_release();
			forcing_shutdown = 0;
		}
		break;

	case POWER_S5:
		if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 1)
			return POWER_S5S3; /* Power up to next state */
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
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}
		break;

	case POWER_G3S5:
		/* Call hooks to initialize PMIC */
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		if (power_wait_signals(IN_PCH_SLP_SUS_DEASSERTED)) {
			chipset_force_shutdown();
			return POWER_G3;
		}

		return POWER_S5;

	case POWER_S5S3:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S5G3;
		}

		/* Enable TP so that it can wake the system */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3S0:
		if (!power_has_signals(IN_PGOOD_ALL_CORE)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		}

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

		/* Suspend wireless */
		wireless_set_state(WIRELESS_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

		return POWER_S5G3;

	case POWER_S5G3:
		return POWER_G3;

	default:
		break;
	}

	return state;
}
