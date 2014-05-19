/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 ivybridge chipset power control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PGOOD_5VALW             POWER_SIGNAL_MASK(X86_PGOOD_5VALW)
#define IN_PGOOD_1_5V_DDR          POWER_SIGNAL_MASK(X86_PGOOD_1_5V_DDR)
#define IN_PGOOD_1_5V_PCH          POWER_SIGNAL_MASK(X86_PGOOD_1_5V_PCH)
#define IN_PGOOD_1_8VS             POWER_SIGNAL_MASK(X86_PGOOD_1_8VS)
#define IN_PGOOD_VCCP              POWER_SIGNAL_MASK(X86_PGOOD_VCCP)
#define IN_PGOOD_VCCSA             POWER_SIGNAL_MASK(X86_PGOOD_VCCSA)
#define IN_PGOOD_CPU_CORE          POWER_SIGNAL_MASK(X86_PGOOD_CPU_CORE)
#define IN_PGOOD_VGFX_CORE         POWER_SIGNAL_MASK(X86_PGOOD_VGFX_CORE)
#define IN_SLP_S3_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_SLP_S4_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)
#define IN_SLP_S5_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S5_DEASSERTED)
#define IN_SLP_A_DEASSERTED        POWER_SIGNAL_MASK(X86_SLP_A_DEASSERTED)
#define IN_SLP_SUS_DEASSERTED      POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)
#define IN_SLP_ME_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_ME_DEASSERTED)

/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_PGOOD_5VALW)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_1_5V_DDR | IN_PGOOD_1_5V_PCH |	\
			      IN_PGOOD_1_8VS | IN_PGOOD_VCCP | IN_PGOOD_VCCSA)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_CPU_CORE | IN_PGOOD_VGFX_CORE)
/* Rails required for S3 */
#define IN_PGOOD_S3          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_1_5V_DDR)
/* Rails required for S0 */
#define IN_PGOOD_S0          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE)

/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_SLP_S3_DEASSERTED |		\
				  IN_SLP_S4_DEASSERTED |		\
				  IN_SLP_S5_DEASSERTED |		\
				  IN_SLP_A_DEASSERTED)
/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE |		\
		   IN_PGOOD_CPU_CORE | IN_ALL_PM_SLP_DEASSERTED)

static int throttle_cpu;      /* Throttle CPU? */

void chipset_force_shutdown(void)
{
	CPRINTS("chipset force shutdown");

	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
}

void chipset_reset(int cold_reset)
{
	if (cold_reset) {
		/*
		 * Drop and restore PWROK.  This causes the PCH to reboot,
		 * regardless of its after-G3 setting.  This type of reboot
		 * causes the PCH to assert PLTRST#, SLP_S3#, and SLP_S5#, so
		 * we actually drop power to the rest of the system (hence, a
		 * "cold" reboot).
		 */

		/* Ignore if PWROK is already low */
		if (gpio_get_level(GPIO_PCH_PWROK) == 0)
			return;

		/* PWROK must deassert for at least 3 RTC clocks = 91 us */
		gpio_set_level(GPIO_PCH_PWROK, 0);
		udelay(100);
		gpio_set_level(GPIO_PCH_PWROK, 1);

	} else {
		/*
		 * Send a RCIN# pulse to the PCH.  This just causes it to
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

void chipset_throttle_cpu(int throttle)
{
	throttle_cpu = throttle;

	/* Immediately set throttling if CPU is on */
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
			CPRINTS("already in S0");
			return POWER_S0;
		} else {
			/* Force all signals to their G3 states */
			CPRINTS("forcing G3");
			gpio_set_level(GPIO_PCH_PWROK, 0);
			gpio_set_level(GPIO_ENABLE_VCORE, 0);
			gpio_set_level(GPIO_ENABLE_VS, 0);
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);
			gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 0);
			gpio_set_level(GPIO_PCH_RSMRST_L, 0);
			gpio_set_level(GPIO_PCH_DPWROK, 0);
		}
	}

	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
		if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 1) {
			/* Power up to next state */
			return POWER_S5S3;
		}
		break;

	case POWER_S3:
		/*
		 * If lid is closed; hold touchscreen in reset to cut power
		 * usage.  If lid is open, take touchscreen out of reset so it
		 * can wake the processor.
		 */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, lid_is_open());

		/* Check for state transitions */
		if (!power_has_signals(IN_PGOOD_S3)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_S0)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S0S3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}
		break;

	case POWER_G3S5:
		/*
		 * Wait 10ms after +3VALW good, since that powers VccDSW and
		 * VccSUS.
		 */
		msleep(10);

		/* Assert DPWROK, deassert RSMRST# */
		gpio_set_level(GPIO_PCH_DPWROK, 1);
		gpio_set_level(GPIO_PCH_RSMRST_L, 1);

		/* Wait 5ms for SUSCLK to stabilize */
		msleep(5);
		return POWER_S5;

	case POWER_S5S3:
		/* Wait for the always-on rails to be good */
		if (power_wait_signals(IN_PGOOD_ALWAYS_ON)) {
			chipset_force_shutdown();
			return POWER_S5;
		}

		/*
		 * Take lightbar out of reset, now that +5VALW is available and
		 * we won't leak +3VALW through the reset line.
		 */
		gpio_set_level(GPIO_LIGHTBAR_RESET_L, 1);

		/* Turn on power to RAM */
		gpio_set_level(GPIO_ENABLE_1_5V_DDR, 1);
		if (power_wait_signals(IN_PGOOD_S3)) {
			chipset_force_shutdown();
			return POWER_S5;
		}

		/*
		 * Enable touchpad power so it can wake the system from
		 * suspend.
		 */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3S0:
		/* Turn on power rails */
		gpio_set_level(GPIO_ENABLE_VS, 1);

		/* Enable wireless */
		wireless_set_state(WIRELESS_ON);

		/*
		 * Make sure touchscreen is out if reset (even if the lid is
		 * still closed); it may have been turned off if the lid was
		 * closed in S3.
		 */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 1);

		/* Wait for non-core power rails good */
		if (power_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown();
			gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
			wireless_set_state(WIRELESS_OFF);
			gpio_set_level(GPIO_ENABLE_VS, 0);
			return POWER_S3;
		}

		/*
		 * Enable +CPU_CORE and +VGFX_CORE regulator.  The CPU itself
		 * will request the supplies when it's ready.
		 */
		gpio_set_level(GPIO_ENABLE_VCORE, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/* Wait 99ms after all voltages good */
		msleep(99);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

		/* Set PCH_PWROK */
		gpio_set_level(GPIO_PCH_PWROK, 1);
		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* Clear PCH_PWROK */
		gpio_set_level(GPIO_PCH_PWROK, 0);

		/* Wait 40ns */
		udelay(1);

		/* Disable +CPU_CORE and +VGFX_CORE */
		gpio_set_level(GPIO_ENABLE_VCORE, 0);

		/* Suspend wireless */
		wireless_set_state(WIRELESS_SUSPEND);

		/*
		 * Deassert prochot since CPU is off and we're about to drop
		 * +VCCP.
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, 0);

		/* Turn off power rails */
		gpio_set_level(GPIO_ENABLE_VS, 0);
		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		/* Disable touchpad power */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

		/* Turn off power to RAM */
		gpio_set_level(GPIO_ENABLE_1_5V_DDR, 0);

		/*
		 * Put touchscreen and lightbar in reset, so we won't leak
		 * +3VALW through the reset line to chips powered by +5VALW.
		 *
		 * (Note that we're no longer powering down +5VALW due to
		 * crosbug.com/p/16600, but to minimize side effects of that
		 * change we'll still reset these components in S5.)
		 */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
		gpio_set_level(GPIO_LIGHTBAR_RESET_L, 0);
		return POWER_S5;

	case POWER_S5G3:
		/* Deassert DPWROK, assert RSMRST# */
		gpio_set_level(GPIO_PCH_DPWROK, 0);
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
		return POWER_G3;
	}

	return state;
}

void power_interrupt(enum gpio_signal signal)
{
	/* Route SUSWARN# back to SUSACK# */
	gpio_set_level(GPIO_PCH_SUSACK_L, gpio_get_level(GPIO_PCH_SUSWARN_L));
}
