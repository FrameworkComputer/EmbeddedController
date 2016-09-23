/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* rk3399 chipset power control module for Chrome EC */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PGOOD_PP5000        POWER_SIGNAL_MASK(PP5000_PWR_GOOD)
#define IN_PGOOD_SYS           POWER_SIGNAL_MASK(SYS_PWR_GOOD)
#define IN_PGOOD_AP            POWER_SIGNAL_MASK(AP_PWR_GOOD)
#define IN_SUSPEND_DEASSERTED  POWER_SIGNAL_MASK(SUSPEND_DEASSERTED)

/* Rails requires for S3 */
#define IN_PGOOD_S3            (IN_PGOOD_PP5000)
/* Rails required for S0 */
#define IN_PGOOD_S0            (IN_PGOOD_S3 | IN_PGOOD_AP | IN_PGOOD_SYS)
/* All inputs in the right state for S0 */
#define IN_ALL_S0              (IN_PGOOD_S0 | IN_SUSPEND_DEASSERTED)

/* Long power key press to force shutdown in S0 */
#define FORCED_SHUTDOWN_DELAY  (8 * SECOND)

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

static int forcing_shutdown;

void chipset_force_shutdown(void)
{
	CPRINTS("%s()", __func__);

	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	forcing_shutdown = 1;
	task_wake(TASK_ID_CHIPSET);
}

void chipset_reset(int cold_reset)
{
	/* Print out the RTC to help correlate resets in logs. */
	print_system_rtc(CC_CHIPSET);

	/* TODO: handle cold_reset */
	CPRINTS("%s(%d)", __func__, cold_reset);

	/* Pulse SYS_RST */
	gpio_set_level(GPIO_SYS_RST_L, 0);
	udelay(90);
	gpio_set_level(GPIO_SYS_RST_L, 1);
}

enum power_state power_chipset_init(void)
{
	if (system_jumped_to_this_image()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
			return POWER_S0;
		}
	} else if (!(system_get_reset_flags() & RESET_FLAG_AP_OFF))
		/* Auto-power on */
		chipset_exit_hard_off();

	return POWER_G3;
}

static void force_shutdown(void)
{
	forcing_shutdown = 1;
	task_wake(TASK_ID_CHIPSET);
}
DECLARE_DEFERRED(force_shutdown);

/*
 * Debounce PGOOD_AP if we lose it suddenly during S0, since output voltage
 * transitions may cause spurious pulses.
 */
#define PGOOD_AP_DEBOUNCE_TIMEOUT (100 * MSEC)

/*
 * The AP informs the EC of its S0 / S3 state through IN_SUSPEND_DEASSERTED /
 * AP_EC_S3_S0_L. Latency between deassertion and power rails coming up must
 * be minimized, so check for deassertion at various stages of our suspend
 * power sequencing, and immediately transition out of suspend if necessary.
 */
#define CHECK_ABORTED_SUSPEND() \
	if (!forcing_shutdown && \
	    power_get_signals() & IN_SUSPEND_DEASSERTED)  { \
		CPRINTS("suspend aborted"); \
		return POWER_S3S0; \
	}

enum power_state power_handle_state(enum power_state state)
{
	static int sys_reset_asserted;
	int tries = 0;

	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
		if (forcing_shutdown)
			return POWER_S5G3;
		else
			return POWER_S5S3;
		break;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_S3) || forcing_shutdown)
			return POWER_S3S5;
		else if (power_get_signals() & IN_SUSPEND_DEASSERTED)
			return POWER_S3S0;
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_S3) ||
		    forcing_shutdown ||
		    !(power_get_signals() & IN_SUSPEND_DEASSERTED))
			return POWER_S0S3;

		/*
		 * Wait up to PGOOD_AP_DEBOUNCE_TIMEOUT for IN_PGOOD_AP to
		 * come back before transitioning back to S3.
		 */
		if (power_wait_signals_timeout(IN_PGOOD_AP,
					       PGOOD_AP_DEBOUNCE_TIMEOUT)
					       == EC_ERROR_TIMEOUT)
			return POWER_S0S3;

		/*
		 * power_wait_signals_timeout() can block and consume task
		 * wake events, so re-verify the state of the world.
		 */
		if (!power_has_signals(IN_PGOOD_S3) ||
		    forcing_shutdown ||
		    !(power_get_signals() & IN_SUSPEND_DEASSERTED))
			return POWER_S0S3;

		break;

	case POWER_G3S5:
		forcing_shutdown = 0;

		/*
		 * Allow time for charger to be initialized, in case we're
		 * trying to boot the AP with no battery.
		 */
		while (charge_prevent_power_on(0) &&
		       tries++ < CHARGER_INITIALIZED_TRIES) {
			msleep(CHARGER_INITIALIZED_DELAY_MS);
		}

		/* Return to G3 if battery level is too low. */
		if (charge_want_shutdown() ||
		    tries > CHARGER_INITIALIZED_TRIES) {
			CPRINTS("power-up inhibited");
			chipset_force_shutdown();
			return POWER_G3;
		}

		/* Power up to next state */
		return POWER_S5;

	case POWER_S5S3:
		gpio_set_level(GPIO_PPVAR_LOGIC_EN, 1);
		gpio_set_level(GPIO_PP900_AP_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP900_PMU_EN, 1);
		gpio_set_level(GPIO_PP900_PLL_EN, 1);
		gpio_set_level(GPIO_PP900_USB_EN, 1);
		gpio_set_level(GPIO_PP900_PCIE_EN, 1);
		msleep(2);

		/*
		 * Assert SYS_RST now, to be released in S3S0, to avoid
		 * resetting the TPM soon after power-on.
		 */
		gpio_set_level(GPIO_SYS_RST_L, 0);
		sys_reset_asserted = 1;

		gpio_set_level(GPIO_PP1800_PMU_EN_L, 0);
		msleep(2);
		/* TODO(crosbug.com/p/55981): De-power CLOGIC in S3 */
		gpio_set_level(GPIO_PPVAR_CLOGIC_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_LPDDR_PWR_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP1800_USB_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_PP3300_USB_EN_L, 0);
		gpio_set_level(GPIO_PP5000_EN, 1);
		gpio_set_level(GPIO_PP3300_TRACKPAD_EN_L, 0);
		msleep(1);
		gpio_set_level(GPIO_PP1800_LID_EN_L, 0);

		/*
		 * TODO: Consider ADC_PP900_AP / ADC_PP1200_LPDDR analog
		 * voltage levels for state transition.
		 */
		if (power_wait_signals(IN_PGOOD_S3)) {
			chipset_force_shutdown();
			return POWER_S3S5;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

		/* Power up to next state */
		return POWER_S3;

	case POWER_S3S0:
		gpio_set_level(GPIO_PP900_DDRPLL_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP1800_AP_AVDD_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_AP_CORE_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP1800_S0_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_PP3300_S0_EN_L, 0);

		/* Release SYS_RST if we came from S5 */
		if (sys_reset_asserted) {
			msleep(10);
			gpio_set_level(GPIO_SYS_RST_L, 1);

			sys_reset_asserted = 0;
		}

		gpio_set_level(GPIO_PP1800_SIXAXIS_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_PP1800_SENSOR_EN_L, 0);

		if (power_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown();
			return POWER_S3S0;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/* Power up to next state */
		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		CHECK_ABORTED_SUSPEND();

		msleep(10);
		CHECK_ABORTED_SUSPEND();

		gpio_set_level(GPIO_PP1800_SENSOR_EN_L, 1);
		gpio_set_level(GPIO_PP1800_SIXAXIS_EN_L, 1);
		gpio_set_level(GPIO_PP3300_S0_EN_L, 1);
		msleep(10);
		CHECK_ABORTED_SUSPEND();

		gpio_set_level(GPIO_PP1800_S0_EN_L, 1);
		msleep(10);
		CHECK_ABORTED_SUSPEND();

		gpio_set_level(GPIO_AP_CORE_EN, 0);
		msleep(10);
		CHECK_ABORTED_SUSPEND();

		gpio_set_level(GPIO_PP1800_AP_AVDD_EN_L, 1);
		msleep(10);
		CHECK_ABORTED_SUSPEND();

		gpio_set_level(GPIO_PP900_DDRPLL_EN, 0);
		msleep(10);
		CHECK_ABORTED_SUSPEND();

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * In case the power button is held awaiting power-off timeout,
		 * power off immediately now that we're entering S3.
		 */
		if (power_button_is_pressed()) {
			forcing_shutdown = 1;
			hook_call_deferred(&force_shutdown_data, -1);
		}

		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		gpio_set_level(GPIO_PP1800_LID_EN_L, 1);
		gpio_set_level(GPIO_PP3300_TRACKPAD_EN_L, 1);
		gpio_set_level(GPIO_PP5000_EN, 0);
		gpio_set_level(GPIO_PP3300_USB_EN_L, 1);
		msleep(10);
		gpio_set_level(GPIO_PP1800_USB_EN_L, 1);
		msleep(2);
		gpio_set_level(GPIO_LPDDR_PWR_EN, 0);
		msleep(2);
		gpio_set_level(GPIO_PPVAR_CLOGIC_EN, 0);
		msleep(10);
		gpio_set_level(GPIO_PP1800_PMU_EN_L, 1);
		msleep(2);
		gpio_set_level(GPIO_PP900_PCIE_EN, 0);
		gpio_set_level(GPIO_PP900_USB_EN, 0);
		gpio_set_level(GPIO_PP900_PLL_EN, 0);
		gpio_set_level(GPIO_PP900_PMU_EN, 0);
		msleep(2);
		gpio_set_level(GPIO_PP900_AP_EN, 0);
		gpio_set_level(GPIO_PPVAR_LOGIC_EN, 0);

		/* Start shutting down */
		return POWER_S5;

	case POWER_S5G3:
		return POWER_G3;
	}

	return state;
}

static void power_button_changed(void)
{
	if (power_button_is_pressed()) {
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			/* Power up from off */
			chipset_exit_hard_off();

		else if (!chipset_in_state(CHIPSET_STATE_ON)) {
			/* Power down immediately from S3 */
			force_shutdown();
			return;
		}

		/* Delayed power down from S0, cancel on PB release */
		hook_call_deferred(&force_shutdown_data,
				   FORCED_SHUTDOWN_DELAY);
	} else {
		/* Power button released, cancel deferred shutdown */
		hook_call_deferred(&force_shutdown_data, -1);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_changed, HOOK_PRIO_DEFAULT);

static void lid_changed(void)
{
	/* Power-up from off on lid open */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF))
		chipset_exit_hard_off();
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_changed, HOOK_PRIO_DEFAULT);
