/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Apollolake chipset power control module for Chrome EC */

#include "apollolake.h"
#include "console.h"
#include "gpio.h"
#include "intel_x86.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

static int forcing_coldreset; /* Forced coldreset in progress? */

__attribute__((weak)) void chipset_do_shutdown(void)
{
	/* Need to implement board specific shutdown */
}

void chipset_force_shutdown(void)
{
	if (!forcing_coldreset)
		CPRINTS("%s()", __func__);

	chipset_do_shutdown();
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown();

	/* Power up the platform again for forced cold reset */
	if (forcing_coldreset) {
		forcing_coldreset = 0;
		return POWER_G3S5;
	}

	return POWER_G3;
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);
	if (cold_reset) {
		/*
		 * Perform chipset_force_shutdown and mark forcing_coldreset.
		 * Once in S5G3 state, check forcing_coldreset to power up.
		 */
		forcing_coldreset = 1;

		chipset_force_shutdown();
	} else {
		/*
		 * Send a pulse to SOC PMU_RSTBTN_N to trigger a warm reset.
		 */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		usleep(32 * MSEC);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

static void handle_all_sys_pgood(enum power_state state)
{
	/*
	 * Pass through asynchronously, as SOC may not react
	 * immediately to power changes.
	 */
	int in_level = gpio_get_level(GPIO_ALL_SYS_PGOOD);
	int out_level = gpio_get_level(GPIO_PCH_SYS_PWROK);

	/* Nothing to do. */
	if (in_level == out_level)
		return;

	gpio_set_level(GPIO_PCH_SYS_PWROK, in_level);

	CPRINTS("Pass through GPIO_ALL_SYS_PGOOD: %d", in_level);
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;

	/* Process ALL_SYS_PGOOD state changes. */
	handle_all_sys_pgood(state);

	if (state == POWER_S5 && !power_has_signals(IN_PGOOD_ALL_CORE)) {
		/* Required rail went away */
		chipset_force_shutdown();

		new_state = POWER_S5G3;
		goto rsmrst_handle;

	} else if (state == POWER_G3S5) {
		/* Platform is powering up, clear forcing_coldreset */
		forcing_coldreset = 0;
	}

	new_state = common_intel_x86_power_handle_state(state);

rsmrst_handle:

	/*
	 * Process RSMRST_L state changes:
	 * RSMRST_L de-assertion is passed to SoC only on G3S5 to S5 transition.
	 * RSMRST_L is also checked in some states and, if asserted, will
	 * force shutdown.
	 */
	common_intel_x86_handle_rsmrst(new_state);

	return new_state;
}

/**
 * chipset check if PLTRST# is valid.
 *
 * @return non-zero if PLTRST# is valid, 0 if invalid.
 */
int chipset_pltrst_is_valid(void)
{
	/*
	 * Invalid PLTRST# from SOC unless RSMRST#
	 * from PMIC through EC to soc is deasserted.
	 */
	return (gpio_get_level(GPIO_RSMRST_L_PGOOD) &&
		gpio_get_level(GPIO_PCH_RSMRST_L));
}
