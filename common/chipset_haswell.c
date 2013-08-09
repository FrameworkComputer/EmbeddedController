/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "chipset.h"
#include "chipset_x86_common.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PGOOD_PP5000            X86_SIGNAL_MASK(X86_PGOOD_PP5000)
#define IN_PGOOD_PP1350            X86_SIGNAL_MASK(X86_PGOOD_PP1350)
#define IN_PGOOD_PP1050            X86_SIGNAL_MASK(X86_PGOOD_PP1050)
#define IN_PGOOD_VCORE             X86_SIGNAL_MASK(X86_PGOOD_VCORE)
#define IN_PCH_SLP_S0n_DEASSERTED  X86_SIGNAL_MASK(X86_PCH_SLP_S0n_DEASSERTED)
#define IN_PCH_SLP_S3n_DEASSERTED  X86_SIGNAL_MASK(X86_PCH_SLP_S3n_DEASSERTED)
#define IN_PCH_SLP_S5n_DEASSERTED  X86_SIGNAL_MASK(X86_PCH_SLP_S5n_DEASSERTED)
#define IN_PCH_SLP_SUSn_DEASSERTED X86_SIGNAL_MASK(X86_PCH_SLP_SUSn_DEASSERTED)

/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_PGOOD_PP5000)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_PP1350 | IN_PGOOD_PP1050)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_VCORE)
/* Rails required for S3 */
#define IN_PGOOD_S3          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_PP1350)
/* Rails required for S0 */
#define IN_PGOOD_S0          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE)

/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3n_DEASSERTED |		\
				  IN_PCH_SLP_S5n_DEASSERTED)
/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE |		\
		   IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

static int throttle_cpu;      /* Throttle CPU? */

void chipset_force_shutdown(void)
{
	CPRINTF("[%T %s()]\n", __func__);

	/*
	 * Force x86 off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
}

void chipset_reset(int cold_reset)
{
	CPRINTF("[%T %s(%d)]\n", __func__, cold_reset);
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

		/*
		 * Pulse must be at least 16 PCI clocks long = 500 ns. The gpio
		 * pin used by the EC (PL6) does not behave in the correct
		 * manner when configured as open drain. In order to mimic
		 * open drain, the pin is initially configured as an input.
		 * When it is needed to drive low, the flags are updated which
		 * changes the pin to an output and drives the pin low.  */
		gpio_set_flags(GPIO_PCH_RCIN_L, GPIO_OUT_LOW);
		udelay(10);
		gpio_set_flags(GPIO_PCH_RCIN_L, GPIO_INPUT);
	}
}

void chipset_throttle_cpu(int throttle)
{
	/* FIXME CPRINTF("[%T %s(%d)]\n", __func__, throttle);*/
}

enum x86_state x86_chipset_init(void)
{
	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_PCH_EDP_VDD_EN);

	/*
	 * If we're switching between images without rebooting, see if the x86
	 * is already powered on; if so, leave it there instead of cycling
	 * through G3.
	 */
	if (system_jumped_to_this_image()) {
		if ((x86_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			CPRINTF("[%T x86 already in S0]\n");
			return X86_S0;
		} else {
			/* Force all signals to their G3 states */
			CPRINTF("[%T x86 forcing G3]\n");
			gpio_set_level(GPIO_PCH_PWROK, 0);
			gpio_set_level(GPIO_VCORE_EN, 0);
			gpio_set_level(GPIO_SUSP_VR_EN, 0);
			gpio_set_level(GPIO_PP1350_EN, 0);
			gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
			gpio_set_level(GPIO_PP3300_DX_EN, 0);
			gpio_set_level(GPIO_PP5000_EN, 0);
			gpio_set_level(GPIO_PCH_RSMRST_L, 0);
			gpio_set_level(GPIO_PCH_DPWROK, 0);
			wireless_enable(0);
		}
	}

	return X86_G3;
}

enum x86_state x86_handle_state(enum x86_state state)
{
	switch (state) {
	case X86_G3:
		break;

	case X86_S5:
		if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 1)
			return X86_S5S3; /* Power up to next state */
		break;

	case X86_S3:
		/* Check for state transitions */
		if (!x86_has_signals(IN_PGOOD_S3)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return X86_S3S5;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
			/* Power up to next state */
			return X86_S3S0;
		} else if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 0) {
			/* Power down to next state */
			return X86_S3S5;
		}
		break;

	case X86_S0:
		if (!x86_has_signals(IN_PGOOD_S0)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return X86_S0S3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return X86_S0S3;
		}
		break;

	case X86_G3S5:
		/*
		 * Wait 10ms after +3VALW good, since that powers VccDSW and
		 * VccSUS.
		 */
		msleep(10);

		/* Assert DPWROK */
		gpio_set_level(GPIO_PCH_DPWROK, 1);
		if (x86_wait_signals(IN_PCH_SLP_SUSn_DEASSERTED)) {
			chipset_force_shutdown();
			return X86_G3;
		}

		gpio_set_level(GPIO_SUSP_VR_EN, 1);
		if (x86_wait_signals(IN_PGOOD_PP1050)) {
			chipset_force_shutdown();
			return X86_G3;
		}

		/* Deassert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 1);

		/* Wait 5ms for SUSCLK to stabilize */
		msleep(5);
		return X86_S5;

	case X86_S5S3:
		/* Enable PP5000 (5V) rail. */
		gpio_set_level(GPIO_PP5000_EN, 1);
		if (x86_wait_signals(IN_PGOOD_PP5000)) {
			chipset_force_shutdown();
			return X86_G3;
		}

		/* Wait for the always-on rails to be good */
		if (x86_wait_signals(IN_PGOOD_ALWAYS_ON)) {
			chipset_force_shutdown();
			return X86_S5G3;
		}

		/* Turn on power to RAM */
		gpio_set_level(GPIO_PP1350_EN, 1);
		if (x86_wait_signals(IN_PGOOD_S3)) {
			chipset_force_shutdown();
			return X86_S5G3;
		}

		/*
		 * Enable touchpad power so it can wake the system from
		 * suspend.
		 */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return X86_S3;

	case X86_S3S0:
		/* Turn on power rails */
		gpio_set_level(GPIO_PP3300_DX_EN, 1);

		/* Enable wireless */
		wireless_enable(EC_WIRELESS_SWITCH_ALL);

		/* Wait for non-core power rails good */
		if (x86_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown();
			wireless_enable(0);
			gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
			gpio_set_level(GPIO_PP3300_DX_EN, 0);
			return X86_S3;
		}

		/*
		 * Enable +CPU_CORE.  The CPU itself will request the supplies
		 * when it's ready.
		 */
		gpio_set_level(GPIO_VCORE_EN, 1);

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
		gpio_set_level(GPIO_SYS_PWROK, 1);
		return X86_S0;

	case X86_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* Clear PCH_PWROK */
		gpio_set_level(GPIO_SYS_PWROK, 0);
		gpio_set_level(GPIO_PCH_PWROK, 0);

		/* Wait 40ns */
		udelay(1);

		/* Disable +CPU_CORE */
		gpio_set_level(GPIO_VCORE_EN, 0);

		/* Disable wireless */
		wireless_enable(0);

		/*
		 * Deassert prochot since CPU is off and we're about to drop
		 * +VCCP.
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, 0);

		/* Turn off power rails */
		gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
		gpio_set_level(GPIO_PP3300_DX_EN, 0);
		return X86_S3;

	case X86_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable touchpad power */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

		/* Turn off power to RAM */
		gpio_set_level(GPIO_PP1350_EN, 0);

		/* Disable PP5000 (5V) rail. */
		gpio_set_level(GPIO_PP5000_EN, 0);
		return X86_S5G3;

	case X86_S5G3:
		/* Deassert DPWROK, assert RSMRST# */
		gpio_set_level(GPIO_PCH_DPWROK, 0);
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
		gpio_set_level(GPIO_SUSP_VR_EN, 0);
		return X86_G3;
	}

	return state;
}

void haswell_interrupt(enum gpio_signal signal)
{
	/* Pass through eDP VDD enable from PCH */
	gpio_set_level(GPIO_EC_EDP_VDD_EN, gpio_get_level(GPIO_PCH_EDP_VDD_EN));
}
