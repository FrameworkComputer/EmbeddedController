/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Icelake chipset power control module for Chrome EC */

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

/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_USEC	(250 * MSEC)

static int forcing_shutdown;  /* Forced shutdown in progress? */

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_DEASSERTED] = {
		.gpio = GPIO_PCH_SLP_S0_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH |
			POWER_SIGNAL_DISABLE_AT_BOOT,
		.name = "SLP_S0_DEASSERTED",
	},
	[X86_SLP_S3_DEASSERTED] = {
		.gpio = SLP_S3_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		.gpio = SLP_S4_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S4_DEASSERTED",
	},
	[X86_SLP_SUS_DEASSERTED] = {
		.gpio = GPIO_SLP_SUS_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_SUS_DEASSERTED",
	},
	[X86_RSMRST_L_PGOOD] = {
		.gpio = GPIO_PG_EC_RSMRST_ODL,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "RSMRST_L_PGOOD",
	},
	[X86_DSW_DPWROK] = {
		.gpio = GPIO_PG_EC_DSW_PWROK,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "DSW_DPWROK",
	},
	[X86_ALL_SYS_PGOOD] = {
		.gpio = GPIO_PG_EC_ALL_SYS_PWRGD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "ALL_SYS_PWRGD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	int timeout_ms = 50;

	CPRINTS("%s() %d", __func__, reason);
	report_ap_reset(reason);

	/* Turn off RMSRST_L  to meet tPCH12 */
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);

	/* Turn off DSW_PWROK to meet tPCH14 */
	gpio_set_level(GPIO_PCH_DSW_PWROK, 0);

	/* Turn off DSW load switch. */
	gpio_set_level(GPIO_EN_PP3300_A, 0);

	/* Turn off PP5000 rail */
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 0);
#else
	gpio_set_level(GPIO_EN_PP5000, 0);
#endif

	/*
	 * TODO(b/111810925): Replace this wait with
	 * power_wait_signals_timeout()
	 */
	/* Now wait for DSW_PWROK and  RSMRST_ODL to go away. */
	while (gpio_get_level(GPIO_PG_EC_DSW_PWROK) &&
	       gpio_get_level(GPIO_PG_EC_RSMRST_ODL) && (timeout_ms > 0)) {
		msleep(1);
		timeout_ms--;
	};

	if (!timeout_ms)
		CPRINTS("DSW_PWROK or RSMRST_ODL didn't go low!  Assuming G3.");
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
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

/*
 * Ice Lake and Tiger Lake permit PCH_PWROK and SYS_PWROK signals coming
 * up in any order.  If the platform needs extra time for peripherals to come
 * up, the board can override this function.
 */
__overridable void board_icl_tgl_all_sys_pwrgood(void)
{

}

enum power_state power_handle_state(enum power_state state)
{
	int dswpwrok_in = gpio_get_level(GPIO_PG_EC_DSW_PWROK);
	static int dswpwrok_out = -1;
	int all_sys_pwrgd_in;
	int all_sys_pwrgd_out;

	/* Pass-through DSW_PWROK to ICL. */
	if (dswpwrok_in != dswpwrok_out) {
		CPRINTS("Pass thru GPIO_DSW_PWROK: %d", dswpwrok_in);
		/*
		 * A minimum 10 msec delay is required between PP3300_A being
		 * stable and the DSW_PWROK signal being passed to the PCH.
		 */
		msleep(10);
		gpio_set_level(GPIO_PCH_DSW_PWROK, dswpwrok_in);
		dswpwrok_out = dswpwrok_in;
	}

	common_intel_x86_handle_rsmrst(state);

	switch (state) {

	case POWER_G3S5:
		/* Turn on PP5000 rail */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 1);
#else
		gpio_set_level(GPIO_EN_PP5000, 1);
#endif

		/*
		 * TODO(b/111121615): Should modify this to wait until the
		 * common power state machine indicates that it's ok to try an
		 * boot the AP prior to turning on the 3300_A rail. This could
		 * be done using chipset_pre_init_callback()
		 */
		/* Turn on the PP3300_DSW rail. */
		gpio_set_level(GPIO_EN_PP3300_A, 1);
		if (power_wait_signals(IN_PGOOD_ALL_CORE))
			break;

		/* Pass thru DSWPWROK again since we changed it. */
		dswpwrok_in = gpio_get_level(GPIO_PG_EC_DSW_PWROK);
		/*
		 * A minimum 10 msec delay is required between PP3300_A being
		 * stable and the DSW_PWROK signal being passed to the PCH.
		 */
		msleep(10);
		gpio_set_level(GPIO_PCH_DSW_PWROK, dswpwrok_in);
		CPRINTS("Pass thru GPIO_DSW_PWROK: %d", dswpwrok_in);
		dswpwrok_out = dswpwrok_in;

		/*
		 * Now wait for SLP_SUS_L to go high based on tPCH32. If this
		 * signal doesn't go high within 250 msec then go back to G3.
		 */
		if (power_wait_signals_timeout(IN_PCH_SLP_SUS_DEASSERTED,
				IN_PCH_SLP_SUS_WAIT_TIME_USEC) != EC_SUCCESS) {
			CPRINTS("SLP_SUS_L didn't go high!  Assuming G3.");
			return POWER_G3;
		}
		break;

	case POWER_S5:
		if (forcing_shutdown) {
			power_button_pch_release();
			forcing_shutdown = 0;
		}
		/* If SLP_SUS_L is asserted, we're no longer in S5. */
		if (!power_has_signals(IN_PCH_SLP_SUS_DEASSERTED))
			return POWER_S5G3;
		break;

	case POWER_S0:
		/*
		 * Check value of PG_EC_ALL_SYS_PWRGD to see if PCH_SYS_PWROK
		 * needs to be changed. If it's low->high transition, call board
		 * specific handling if provided.
		 */
		all_sys_pwrgd_in = gpio_get_level(GPIO_PG_EC_ALL_SYS_PWRGD);
		all_sys_pwrgd_out = gpio_get_level(GPIO_PCH_SYS_PWROK);

		if (all_sys_pwrgd_in != all_sys_pwrgd_out) {
			if (all_sys_pwrgd_in)
				board_icl_tgl_all_sys_pwrgood();
			gpio_set_level(GPIO_PCH_SYS_PWROK, all_sys_pwrgd_in);
		}
		break;

	default:
		break;
	}

	return common_intel_x86_power_handle_state(state);
}
