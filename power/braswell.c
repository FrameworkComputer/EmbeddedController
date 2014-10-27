/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 braswell chipset power control module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "lpc.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"
#include "wireless.h"
#include "registers.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_RSMRST_L_PWRGD          POWER_SIGNAL_MASK(X86_RSMRST_L_PWRGD)
#define IN_ALL_SYS_PWRGD           POWER_SIGNAL_MASK(X86_ALL_SYS_PWRGD)
#define IN_SLP_S3_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_SLP_S4_DEASSERTED       POWER_SIGNAL_MASK(X86_SLP_S4_DEASSERTED)

/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_RSMRST_L_PWRGD)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_ALL_SYS_PWRGD)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_ALL_SYS_PWRGD)
/* Rails required for S5 */
#define IN_PGOOD_S5          (IN_PGOOD_ALWAYS_ON)
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
	CPRINTS("%s()", __func__);

	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_SYS_PWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);
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
		 * PLTRST# to reset the rest of the system.  The PCH uses a 16
		 * ms debounce time, so assert the signal for twice that.
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
		if ((power_get_signals() & IN_PGOOD_S0) == IN_PGOOD_S0) {
			/* Disable idle task deep sleep when in S0. */
			disable_sleep(SLEEP_MASK_AP_RUN);

			CPRINTS("already in S0");
			return POWER_S0;
		} else {
			/* Force all signals to their G3 states */
			CPRINTS("forcing G3");
			gpio_set_level(GPIO_PCH_SYS_PWROK, 0);
			gpio_set_level(GPIO_PCH_RSMRST_L, 0);

			/*wireless_set_state(WIRELESS_OFF);*/
		}
	}

	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_G3S5:
		if (power_wait_signals(IN_PGOOD_S5)) {
			chipset_force_shutdown();
			return POWER_G3;
		}

		/* Deassert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 1);
		return POWER_S5;

	case POWER_S5:
		/* Check for SLP S4 */
		if (gpio_get_level(GPIO_PCH_SLP_S4_L) == 1)
			return POWER_S5S3; /* Power up to next state */
		break;

	case POWER_S5S3:

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

		return POWER_S3;


	case POWER_S3:

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

	case POWER_S3S0:
		/* Enable wireless */

		/*wireless_set_state(WIRELESS_ON);*/

		if (power_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown();

		/*wireless_set_state(WIRELESS_OFF);*/
			return POWER_S3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * Wait 15 ms after all voltages good.  100 ms is only needed
		 * for PCIe devices; mini-PCIe devices should need only 10 ms.
		 */
		msleep(15);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

		/* Set SYS and CORE PWROK */
		gpio_set_level(GPIO_PCH_SYS_PWROK, 1);

		/* Wait 50 ms for platform reset to deassert */
		{
		  int i = 0;
		  CPRINTS("power wait for PLTRST# to deassert");
		  while (lpc_get_pltrst_asserted()) {
			usleep(MSEC);
#ifndef STRAGO_PO
			i++;
#endif
			if (i >= 50) {
				CPRINTS("power timeout on PLTRST#");
				chipset_force_shutdown();

				/*wireless_set_state(WIRELESS_OFF);*/
				return POWER_S3;
			}
		  }
		}

		return POWER_S0;


	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_S0)) {
			/* Required rail went away - Cold Reset? */
			chipset_force_shutdown();
			return POWER_S0S3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}

		break;
	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* Clear SYS and CORE PWROK */
		gpio_set_level(GPIO_PCH_SYS_PWROK, 0);

		/* Wait 40ns */
		udelay(1);

		/* Suspend wireless */

		/*wireless_set_state(WIRELESS_SUSPEND);*/

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

		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/*wireless_set_state(WIRELESS_OFF);*/

		/* Start shutting down */
		return pause_in_s5 ? POWER_S5 : POWER_S5G3;

	case POWER_S5G3:
		/* Assert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
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
