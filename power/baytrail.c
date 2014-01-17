/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 baytrail chipset power control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PGOOD_PP5000            POWER_SIGNAL_MASK(X86_PGOOD_PP5000)
#define IN_PGOOD_PP1050            POWER_SIGNAL_MASK(X86_PGOOD_PP1050)
#define IN_PGOOD_S5                POWER_SIGNAL_MASK(X86_PGOOD_S5)
#define IN_PGOOD_VCORE             POWER_SIGNAL_MASK(X86_PGOOD_VCORE)
#define IN_SLP_S3_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_SLP_S4_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)

/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_PGOOD_S5)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_PP5000)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_VCORE)
/* Rails required for S3 */
#define IN_PGOOD_S3          (IN_PGOOD_ALWAYS_ON)
/* Rails required for S0 */
#define IN_PGOOD_S0          (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE)

/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_SLP_S3_DEASSERTED | IN_SLP_S4_DEASSERTED)
/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE |		\
		   IN_PGOOD_ALL_CORE | IN_ALL_PM_SLP_DEASSERTED)

static int throttle_cpu;      /* Throttle CPU? */
static int pause_in_s5 = 1;   /* Pause in S5 when shutting down? */

void chipset_force_shutdown(void)
{
	CPRINTF("[%T %s()]\n", __func__);

	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_SYS_PWROK, 0);
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
		if (gpio_get_level(GPIO_PCH_SYS_PWROK) == 0)
			return;

		/* PWROK must deassert for at least 3 RTC clocks = 91 us */
		gpio_set_level(GPIO_PCH_SYS_PWROK, 0);
		udelay(100);
		gpio_set_level(GPIO_PCH_SYS_PWROK, 1);

	} else {
		/*
		 * Send a reset pulse to the PCH.  This just causes it to
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.  Pulse must be at
		 * least 16 PCI clocks long = 500 ns.
		 */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
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

			CPRINTF("[%T already in S0]\n");
			return POWER_S0;
		} else {
			/* Force all signals to their G3 states */
			CPRINTF("[%T forcing G3]\n");
			gpio_set_level(GPIO_PCH_CORE_PWROK, 0);
			gpio_set_level(GPIO_VCORE_EN, 0);
			gpio_set_level(GPIO_SUSP_VR_EN, 0);
			gpio_set_level(GPIO_PP1350_EN, 0);
			gpio_set_level(GPIO_PP3300_DX_EN, 0);
			gpio_set_level(GPIO_PP5000_EN, 0);
			gpio_set_level(GPIO_PCH_RSMRST_L, 0);
			gpio_set_level(GPIO_PCH_SYS_PWROK, 0);
			wireless_enable(0);
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
		if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 1)
			return POWER_S5S3; /* Power up to next state */
		break;

	case POWER_S3:
		/*
		 * If lid is closed; hold touchscreen in reset to cut power
		 * usage.  If lid is open, take touchscreen out of reset so it
		 * can wake the processor. Chipset task is awakened on lid
		 * switch transitions.
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
		} else if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 0) {
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

		gpio_set_level(GPIO_SUSP_VR_EN, 1);
		if (power_wait_signals(IN_PGOOD_S5)) {
			chipset_force_shutdown();
			return POWER_G3;
		}

		/* Deassert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 1);

		/* Wait 10ms for SUSCLK to stabilize */
		msleep(10);
		return POWER_S5;

	case POWER_S5S3:
		/* Wait for the always-on rails to be good */
		if (power_wait_signals(IN_PGOOD_ALWAYS_ON)) {
			chipset_force_shutdown();
			return POWER_S5G3;
		}

		/* Turn on power to RAM */
		gpio_set_level(GPIO_PP1350_EN, 1);
		if (power_wait_signals(IN_PGOOD_S3)) {
			chipset_force_shutdown();
			return POWER_S5G3;
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
		gpio_set_level(GPIO_PP5000_EN, 1);
		gpio_set_level(GPIO_PP3300_DX_EN, 1);

		/* Enable wireless */
		wireless_enable(EC_WIRELESS_SWITCH_ALL);

		/*
		 * Make sure touchscreen is out if reset (even if the lid is
		 * still closed); it may have been turned off if the lid was
		 * closed in S3.
		 */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 1);

		/* Wait for non-core power rails good */
		if (power_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown();
			wireless_enable(0);
			gpio_set_level(GPIO_PP3300_DX_EN, 0);
			gpio_set_level(GPIO_PP5000_EN, 0);
			gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
			return POWER_S3;
		}

		/*
		 * Enable +CPU_CORE.  The CPU itself will request the supplies
		 * when it's ready.
		 */
		gpio_set_level(GPIO_VCORE_EN, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/* Wait 100ms after all voltages good */
		msleep(100);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

		/* Set SYS and CORE PWROK */
		gpio_set_level(GPIO_PCH_SYS_PWROK, 1);
		gpio_set_level(GPIO_PCH_CORE_PWROK, 1);
		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* Clear SYS and CORE PWROK */
		gpio_set_level(GPIO_PCH_SYS_PWROK, 0);
		gpio_set_level(GPIO_PCH_CORE_PWROK, 0);

		/* Wait 40ns */
		udelay(1);

		/* Disable +CPU_CORE */
		gpio_set_level(GPIO_VCORE_EN, 0);

#ifdef CONFIG_WIRELESS_SUSPEND_ENABLE_WIFI
		/* Disable WWAN, but leave WiFi on */
		wireless_enable(EC_WIRELESS_SWITCH_WLAN |
				EC_WIRELESS_SWITCH_WLAN_POWER);
#else
		/* Disable wireless */
		wireless_enable(0);
#endif

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * Deassert prochot since CPU is off and we're about to drop
		 * +VCCP.
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, 0);

		/* Turn off power rails */
		gpio_set_level(GPIO_PP3300_DX_EN, 0);
		gpio_set_level(GPIO_PP5000_EN, 0);
		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_enable(0);

		/* Disable touchpad power */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

		/* Turn off power to RAM */
		gpio_set_level(GPIO_PP1350_EN, 0);

		/* Start shutting down */
		return pause_in_s5 ? POWER_S5 : POWER_S5G3;

	case POWER_S5G3:
		/* Assert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
		gpio_set_level(GPIO_SUSP_VR_EN, 0);

		return POWER_G3;
	}

	return state;
}

static int host_command_gsv(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_set_value *p = args->params;
	struct ec_response_get_set_value *r = args->response;

	if (p->flags & EC_GSV_SET)
		pause_in_s5 = p->value;

	r->value = pause_in_s5;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GSV_PAUSE_IN_S5,
		     host_command_gsv,
		     EC_VER_MASK(0));

static int console_command_gsv(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &pause_in_s5))
		return EC_ERROR_INVAL;

	ccprintf("pause_in_s5 = %s\n", pause_in_s5 ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pause_in_s5, console_command_gsv,
			"[on|off]",
			"Should the AP pause in S5 during shutdown?",
			NULL);

