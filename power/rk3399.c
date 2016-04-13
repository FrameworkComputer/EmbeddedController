/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* rk3399 chipset power control module for Chrome EC */

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
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
/* TODO: Monitor input signals to determine AP power state */

static const struct power_signal_info power_control_outputs[] = {
	{ GPIO_AP_CORE_EN, 1 },
	{ GPIO_LPDDR_PWR_EN, 1 },
	{ GPIO_PPVAR_CLOGIC_EN, 1 },
	{ GPIO_PPVAR_LOGIC_EN, 1 },

	{ GPIO_PP900_AP_EN, 1 },
	{ GPIO_PP900_DDRPLL_EN, 1 },
	{ GPIO_PP900_PLL_EN, 1 },
	{ GPIO_PP900_PMU_EN, 1 },
	{ GPIO_PP900_USB_EN, 1 },
	{ GPIO_PP900_PCIE_EN, 1 },

	{ GPIO_PP1200_HSIC_EN, 1 },

	{ GPIO_PP1800_SENSOR_EN_L, 0 },
	{ GPIO_PP1800_LID_EN_L, 0 },
	{ GPIO_PP1800_PMU_EN_L, 0 },
	{ GPIO_PP1800_AP_AVDD_EN_L, 0 },
	{ GPIO_PP1800_USB_EN_L, 0 },
	{ GPIO_PP1800_S0_EN_L, 0 },
	{ GPIO_PP1800_SIXAXIS_EN_L, 0 },

	{ GPIO_PP3300_TRACKPAD_EN_L, 0 },
	{ GPIO_PP3300_USB_EN_L, 0 },
	{ GPIO_PP3300_S0_EN_L, 0 },

	{ GPIO_PP5000_EN, 1 },

	{ GPIO_SYS_RST_L, 0 },
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

void chipset_reset(int cold_reset)
{
	/* TODO: handle cold_reset */
	CPRINTS("%s(%d)", __func__, cold_reset);

	/* Pulse SYS_RST */
	gpio_set_level(GPIO_SYS_RST_L, 0);
	udelay(10);
	gpio_set_level(GPIO_SYS_RST_L, 1);
}

enum power_state power_chipset_init(void)
{
	int i;
	const struct power_signal_info *output_signal;

	/* TODO: decode state after sysjump */
	/* Force all signals to their G3 states */
	CPRINTS("forcing G3");
	for (i = 0; i < ARRAY_SIZE(power_control_outputs); ++i) {
		output_signal = &power_control_outputs[i];
		gpio_set_level(output_signal->gpio, !output_signal->level);
	}

	wireless_set_state(WIRELESS_OFF);
	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
		if (forcing_shutdown)
			return POWER_S5G3;
		else
			return POWER_S5S3;

	case POWER_S3:
		if (forcing_shutdown)
			return POWER_S3S5;
		else
			return POWER_S3S0;

	case POWER_S0:
		if (forcing_shutdown)
			return POWER_S0S3;
		break;

	case POWER_G3S5:
		forcing_shutdown = 0;

		/* Power up to next state */
		return POWER_S5;

	case POWER_S5S3:
		gpio_set_level(GPIO_PPVAR_LOGIC_EN, 1);
		gpio_set_level(GPIO_PP900_AP_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP900_PMU_EN, 1);
		gpio_set_level(GPIO_PP900_PLL_EN, 1);
		gpio_set_level(GPIO_PP900_USB_EN, 1);
		gpio_set_level(GPIO_PP900_DDRPLL_EN, 1);
		gpio_set_level(GPIO_PP900_PCIE_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PPVAR_CLOGIC_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP1800_PMU_EN_L, 0);
		gpio_set_level(GPIO_PP1800_USB_EN_L, 0);
		gpio_set_level(GPIO_PP1800_AP_AVDD_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_LPDDR_PWR_EN, 1);
		gpio_set_level(GPIO_PP5000_EN, 1);
		msleep(2);

		gpio_set_level(GPIO_PP1800_SIXAXIS_EN_L, 0);
		gpio_set_level(GPIO_PP3300_TRACKPAD_EN_L, 0);
		gpio_set_level(GPIO_PP1200_HSIC_EN, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		/* Power up to next state */
		return POWER_S3;

	case POWER_S3S0:
		gpio_set_level(GPIO_AP_CORE_EN, 1);
		msleep(2);
		gpio_set_level(GPIO_PP3300_USB_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_PP1800_S0_EN_L, 0);
		msleep(2);
		gpio_set_level(GPIO_PP3300_S0_EN_L, 0);
		msleep(2);
		msleep(10); /* TBD */

		/* Pulse SYS_RST */
		gpio_set_level(GPIO_SYS_RST_L, 0);
		msleep(10);
		gpio_set_level(GPIO_SYS_RST_L, 1);

		gpio_set_level(GPIO_PP1800_LID_EN_L, 0);
		gpio_set_level(GPIO_PP1800_SENSOR_EN_L, 0);

		/* Enable wireless */
		wireless_set_state(WIRELESS_ON);

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

		/* Start shutting down */
		return POWER_S5;

	case POWER_S5G3:
		/* Initialize power signal outputs to default. */
		return power_chipset_init();
	}

	return state;
}

static void power_button_changed(void)
{
	/* Only pay attention to power button presses, not releases */
	if (!power_button_is_pressed())
		return;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		/* Power up */
		chipset_exit_hard_off();
	else
		forcing_shutdown = 1;

	task_wake(TASK_ID_CHIPSET);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_changed, HOOK_PRIO_DEFAULT);
