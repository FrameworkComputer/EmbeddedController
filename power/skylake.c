/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skylake IMVP8 / ROP PMIC chipset power control module for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "intel_x86.h"
#include "lpc.h"
#include "power_button.h"
#include "skylake.h"
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
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		forcing_shutdown = 1;
		power_button_pch_press();
	}
}

__attribute__((weak)) void chipset_set_pmic_slp_sus_l(int level)
{
	gpio_set_level(GPIO_PMIC_SLP_SUS_L, level);
}

enum power_state chipset_force_g3(void)
{
	CPRINTS("Forcing fake G3.");

	chipset_set_pmic_slp_sus_l(0);

	return POWER_G3;
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);

	if (cold_reset) {
		if (gpio_get_level(GPIO_SYS_RESET_L) == 0)
			return;
		gpio_set_level(GPIO_SYS_RESET_L, 0);
		/* Debounce time for SYS_RESET_L is 16 ms */
		udelay(20 * MSEC);
		gpio_set_level(GPIO_SYS_RESET_L, 1);
	} else {
		/*
		 * Send a RCIN_PCH_RCIN_L
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
#ifdef CONFIG_ESPI_VW_SIGNALS
		lpc_host_reset();
#else
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
#endif
	}
}

static void handle_slp_sus(enum power_state state)
{
	/* If we're down or going down don't do anythin with SLP_SUS_L. */
	if (state == POWER_G3 || state == POWER_S5G3)
		return;

	/* Always mimic PCH SLP_SUS request for all other states. */
	chipset_set_pmic_slp_sus_l(gpio_get_level(GPIO_PCH_SLP_SUS_L));
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;

	/* Process RSMRST_L state changes. */
	common_intel_x86_handle_rsmrst(state);

	if (state == POWER_S5 && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}

	new_state = common_intel_x86_power_handle_state(state);

	/* Process SLP_SUS_L state changes after a new state is decided. */
	handle_slp_sus(new_state);

	return new_state;
}
