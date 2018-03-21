/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cannonlake chipset power control module for Chrome EC */

#include "cannonlake.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "intel_x86.h"
#include "power.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

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
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		forcing_shutdown = 1;
		power_button_pch_press();
	}
}

void chipset_handle_espi_reset_assert(void)
{
	/*
	 * If eSPI_Reset# pin is asserted without SLP_SUS# being asserted, then
	 * it means that there is an unexpected power loss (global reset
	 * event). In this case, check if shutdown was being forced by pressing
	 * power button. If yes, release power button.
	 */
	if ((power_get_signals() & IN_PCH_SLP_SUS_DEASSERTED) &&
		forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}
}

enum power_state chipset_force_g3(void)
{
	int timeout = 50;
	chipset_force_shutdown();

	/* Turn off DSW load switch. */
	gpio_set_level(GPIO_EN_PP3300_DSW, 0);

	/* Now wait for DSW_PWROK to go away. */
	while (gpio_get_level(GPIO_PMIC_DPWROK) && (timeout > 0)) {
		msleep(1);
		timeout--;
	};

	if (!timeout)
		CPRINTS("DSW_PWROK didn't go low!  Assuming G3.");

	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;
	int dswpwrok_in = gpio_get_level(GPIO_PMIC_DPWROK);
	static int dswpwrok_out = -1;

	/* Pass-through DSW_PWROK to CNL. */
	if (dswpwrok_in != dswpwrok_out) {
		CPRINTS("Pass thru GPIO_DSW_PWROK: %d", dswpwrok_in);
		gpio_set_level(GPIO_PCH_DSW_PWROK, dswpwrok_in);
		dswpwrok_out = dswpwrok_in;
	}

	common_intel_x86_handle_rsmrst(state);

	if (state == POWER_S5 && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}

	switch (state) {
	case POWER_G3:
		/* If SLP_SUS_L is deasserted, we're no longer in G3. */
		if (power_has_signals(IN_PCH_SLP_SUS_DEASSERTED))
			return POWER_S5;
		break;

	case POWER_G3S5:
		/* Turn on the PP3300_DSW rail. */
		gpio_set_level(GPIO_EN_PP3300_DSW, 1);
		if (power_wait_signals(IN_PGOOD_ALL_CORE))
			break;

		/* Pass thru DSWPWROK again since we changed it. */
		dswpwrok_in = gpio_get_level(GPIO_PMIC_DPWROK);
		gpio_set_level(GPIO_PCH_DSW_PWROK, dswpwrok_in);
		CPRINTS("Pass thru GPIO_DSW_PWROK: %d", dswpwrok_in);
		dswpwrok_out = dswpwrok_in;

		/* Enable the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 1);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
		gpio_set_level(GPIO_EN_PP5000, 1);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */
		break;

	case POWER_S5G3:
		/* Turn off the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 0);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
		gpio_set_level(GPIO_EN_PP5000, 0);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */
		break;

	default:
		break;
	};

	new_state = common_intel_x86_power_handle_state(state);

	return new_state;
}
