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

void chipset_reset(int cold_reset)
{
	/*
	 * The EC cannot control warm vs cold reset of the chipset using
	 * SYS_RESET_L;  it's more of a request.
	 */
	CPRINTS("%s()", __func__);

	if (gpio_get_level(GPIO_SYS_RESET_L) == 0)
		return;

	/*
	 * Debounce time for SYS_RESET_L is 16 ms.  Wait twice that period to be
	 * safe.
	 */
	gpio_set_level(GPIO_SYS_RESET_L, 0);
	udelay(32 * MSEC);
	gpio_set_level(GPIO_SYS_RESET_L, 1);
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown();

	CPRINTS("Faking G3.  (NOOP for now.)");
	/* TODO(aaboagye): Do the right thing for real. */
	/* TODO(aaboagye): maybe turn off DSW load switch. */
	return POWER_G3;
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;
	int dswpwrok_in = gpio_get_level(GPIO_PMIC_DPWROK);

	/* Pass-through DSW_PWROK to CNL. */
	gpio_set_level(GPIO_PCH_DSW_PWROK, dswpwrok_in);
	CPRINTS("Pass thru GPIO_DSW_PWROK: %d", dswpwrok_in);

	common_intel_x86_handle_rsmrst(state);

	if (state == POWER_S5 && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}

	/* TODO(aaboagye): Enable 3300_DSW in Deep Sx only. */
	switch (state) {
	case POWER_S5S3:
		/*
		 * In S3, enable 5V rail.  Wireless rails are handled by common
		 * x86 chipset code.
		 */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 1);
#else
		gpio_set_level(GPIO_EN_PP5000, 1);
#endif
		break;

	case POWER_S3S5:
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 0);
#else
		gpio_set_level(GPIO_EN_PP5000, 0);
#endif
		break;

	default:
		break;
	};

	new_state = common_intel_x86_power_handle_state(state);

	return new_state;
}
