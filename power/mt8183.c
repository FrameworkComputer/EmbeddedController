/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* mt8183 chipset power control module for Chrome EC */

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
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PGOOD_PMIC		POWER_SIGNAL_MASK(PMIC_PWR_GOOD)
#define IN_SUSPEND_DEASSERTED	POWER_SIGNAL_MASK(AP_IN_S3_L)

/* Rails required for S3 and S0 */
#define IN_PGOOD_S0		(IN_PGOOD_PMIC)
#define IN_PGOOD_S3		(IN_PGOOD_PMIC)

/* All inputs in the right state for S0 */
#define IN_ALL_S0		(IN_PGOOD_S0 | IN_SUSPEND_DEASSERTED)

/* Long power key press to force shutdown in S0 */
#define FORCED_SHUTDOWN_DELAY	(8 * SECOND)

#define CHARGER_INITIALIZED_DELAY_MS 100
#define CHARGER_INITIALIZED_TRIES 40

#define PMIC_EN_PULSE_MS 50

/* Data structure for a GPIO operation for power sequencing */
struct power_seq_op {
	/* enum gpio_signal in 8 bits */
	uint8_t signal;
	uint8_t level;
	/* Number of milliseconds to delay after setting signal to level */
	uint8_t delay;
};
BUILD_ASSERT(GPIO_COUNT < 256);

/*
 * This is the power sequence for POWER_S5S3.
 * The entries in the table are handled sequentially from the top
 * to the bottom.
 */

static const struct power_seq_op s5s3_power_seq[] = {
	{ GPIO_PP3300_S3_EN, 1, 2 },
	{ GPIO_PP1800_S3_EN, 1, 2 },
	/* Turn on AP. */
	{ GPIO_AP_SYS_RST_L, 1, 2 },
};

/* The power sequence for POWER_S3S0 */
static const struct power_seq_op s3s0_power_seq[] = {
	{ GPIO_PP3300_S0_EN, 1, 0 },
	{ GPIO_PP1800_S0_EN, 1, 0 },
};

/* The power sequence for POWER_S0S3 */
static const struct power_seq_op s0s3_power_seq[] = {
	{ GPIO_PP3300_S0_EN, 0, 0 },
	{ GPIO_PP1800_S0_EN, 0, 0 },
};

/* The power sequence for POWER_S3S5 */
static const struct power_seq_op s3s5_power_seq[] = {
	/* Turn off AP. */
	{ GPIO_AP_SYS_RST_L, 0, 0 },
	{ GPIO_PP1800_S3_EN, 0, 2 },
	{ GPIO_PP3300_S3_EN, 0, 2 },
	/* Pulse watchdog to PMIC (there may be a 1.6ms debounce) */
	{ GPIO_PMIC_WATCHDOG_L, 0, 3 },
	{ GPIO_PMIC_WATCHDOG_L, 1, 0 },
};

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
DECLARE_DEFERRED(chipset_force_shutdown);

/* If chipset needs to be reset, EC also reboots to RO. */
void chipset_reset(void)
{
	CPRINTS("%s", __func__);

	cflush();
	system_reset(SYSTEM_RESET_HARD);

	/* This should not be reachable. */
	while (1)
		;
}

enum power_state power_chipset_init(void)
{
	if (system_jumped_to_this_image()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
			return POWER_S0;
		}
	} else if (!(system_get_reset_flags() & RESET_FLAG_AP_OFF)) {
		/* Auto-power on */
		chipset_exit_hard_off();
		/*
		 * TODO(b:109850749): If we see that PMIC power good is up,
		 * we could probably jump straight to S5 and power on the AP.
		 */
	}

	return POWER_G3;
}

/**
 * Step through the power sequence table and do corresponding GPIO operations.
 *
 * @param	power_seq_ops	The pointer to the power sequence table.
 * @param	op_count	The number of entries of power_seq_ops.
 */
static void power_seq_run(const struct power_seq_op *power_seq_ops,
			  int op_count)
{
	int i;

	for (i = 0; i < op_count; i++) {
		gpio_set_level(power_seq_ops[i].signal,
			       power_seq_ops[i].level);
		if (!power_seq_ops[i].delay)
			continue;
		msleep(power_seq_ops[i].delay);
	}
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
		if (forcing_shutdown) {
			/*
			 * While PMIC is still not off, press power+home button.
			 * This should not happen if PMIC is configured
			 * properly, and shuts down upon receiving WATCHDOG.
			 */
			if (power_has_signals(IN_PGOOD_PMIC)) {
				gpio_set_level(GPIO_PMIC_EN_ODL, 0);
				return POWER_S5;
			}

			gpio_set_level(GPIO_PMIC_EN_ODL, 1);
			return POWER_S5G3;
		} else {
			return POWER_S5S3;
		}
		break;

	case POWER_S3:
		if (!power_has_signals(IN_PGOOD_S3) || forcing_shutdown)
			return POWER_S3S5;
		else if (power_get_signals() & IN_SUSPEND_DEASSERTED)
			return POWER_S3S0;
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_S0) ||
		    forcing_shutdown ||
		    !(power_get_signals() & IN_SUSPEND_DEASSERTED))
			return POWER_S0S3;

		break;

	case POWER_G3S5:
		forcing_shutdown = 0;

		/* If PMIC is off, switch it on by pulsing PMIC enable. */
		if (!power_has_signals(IN_PGOOD_PMIC)) {
			gpio_set_level(GPIO_PMIC_EN_ODL, 1);
			msleep(PMIC_EN_PULSE_MS);
			gpio_set_level(GPIO_PMIC_EN_ODL, 0);
		}

		/* If EC is in RW, reboot to RO. */
		if (system_get_image_copy() != SYSTEM_IMAGE_RO) {
			/*
			 * TODO(b:109850749): How quickly does the EC come back
			 * up? Would IN_PGOOD_PMIC be ready by the time we are
			 * back? According to PMIC spec, it should take ~158 ms
			 * after debounce (32 ms), minus PMIC_EN_PULSE_MS above.
			 * It would be good to avoid another _EN pulse above.
			 */
			chipset_reset();
		}

		/* Wait for PMIC to bring up rails. */
		if (power_wait_signals(IN_PGOOD_PMIC))
			return POWER_G3;

		/* Power up to next state */
		return POWER_S5;

	case POWER_S5S3:
		/* Enable S3 power supplies, release AP reset. */
		power_seq_run(s5s3_power_seq, ARRAY_SIZE(s5s3_power_seq));

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

		/* Power up to next state */
		return POWER_S3;

	case POWER_S3S0:
		power_seq_run(s3s0_power_seq, ARRAY_SIZE(s3s0_power_seq));

		if (power_wait_signals(IN_PGOOD_S0)) {
			chipset_force_shutdown();
			return POWER_S0S3;
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

		/*
		 * TODO(b:109850749): Check if we need some delay here to
		 * "debounce" entering suspend (rk3399 uses 20ms delay).
		 */

		power_seq_run(s0s3_power_seq, ARRAY_SIZE(s0s3_power_seq));

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
			hook_call_deferred(&chipset_force_shutdown_data, -1);
		}

		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		power_seq_run(s3s5_power_seq, ARRAY_SIZE(s3s5_power_seq));

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

		/* Delayed power down from S0/S3, cancel on PB release */
		hook_call_deferred(&chipset_force_shutdown_data,
				   FORCED_SHUTDOWN_DELAY);
	} else {
		/* Power button released, cancel deferred shutdown */
		hook_call_deferred(&chipset_force_shutdown_data, -1);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_changed, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_LID_SWITCH
static void lid_changed(void)
{
	/* Power-up from off on lid open */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF))
		chipset_exit_hard_off();
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_changed, HOOK_PRIO_DEFAULT);
#endif
