/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Meteorlake chipset power control module for Chrome EC
 * TODO(b/223985632): Use native Zephyr power sequencing once implemented.
 */

#include "board_config.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level(signal, value)
#endif

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
	[X86_SLP_S5_DEASSERTED] = {
		.gpio = SLP_S5_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_RSMRST_L_PGOOD] = {
		.gpio = GPIO_PG_EC_RSMRST_ODL,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "RSMRST_L_PGOOD",
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
	board_before_rsmrst(0);
	GPIO_SET_LEVEL(GPIO_PCH_RSMRST_L, 0);
	board_after_rsmrst(0);

	/* Turn off PRIM load switch. */
	GPIO_SET_LEVEL(GPIO_EN_PP3300_A, 0);

	/* Turn off PP5000 rail */
	if (IS_ENABLED(CONFIG_POWER_PP5000_CONTROL))
		power_5v_enable(task_get_current(), 0);
	else
		GPIO_SET_LEVEL(GPIO_EN_PP5000, 0);

	/* RSMRST_ODL to go away. */
	while (gpio_get_level(GPIO_PG_EC_RSMRST_ODL) && (timeout_ms > 0)) {
		msleep(1);
		timeout_ms--;
	};

	if (!timeout_ms)
		CPRINTS("RSMRST_ODL didn't go low!  Assuming G3.");
}

void chipset_handle_espi_reset_assert(void)
{
	/* Nothing to do */
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

static void enable_pp5000_rail(void)
{
	if (IS_ENABLED(CONFIG_POWER_PP5000_CONTROL))
		power_5v_enable(task_get_current(), 1);
	else
		GPIO_SET_LEVEL(GPIO_EN_PP5000, 1);

}

/*
 * Set the PWROK signal state
 *
 * &param level		0 deasserts the signal, other values assert the signal
 */
static void pwrok_signal_set(const struct intel_x86_pwrok_signal *signal,
	int level)
{
	GPIO_SET_LEVEL(signal->gpio, signal->active_low ? !level : level);
}

__overridable int intel_x86_get_pg_ec_all_sys_pwrgd(void)
{
	return gpio_get_level(GPIO_PG_EC_ALL_SYS_PWRGD);
}

/*
 * Pass through the state of the ALL_SYS_PWRGD input to all the PWROK outputs
 * defined by the board.
 */
static void all_sys_pwrgd_pass_thru(void)
{
	int all_sys_pwrgd_in = intel_x86_get_pg_ec_all_sys_pwrgd();
	const struct intel_x86_pwrok_signal *pwrok_signal;
	int signal_count;
	int i;

	if (all_sys_pwrgd_in) {
		pwrok_signal = pwrok_signal_assert_list;
		signal_count = pwrok_signal_assert_count;
	} else {
		pwrok_signal = pwrok_signal_deassert_list;
		signal_count = pwrok_signal_deassert_count;
	}

	/*
	 * Loop through all PWROK signals defined by the board and set
	 * to match the current ALL_SYS_PWRGD input.
	 */
	for (i = 0; i < signal_count; i++, pwrok_signal++) {
		if (pwrok_signal->delay_ms > 0)
			msleep(pwrok_signal->delay_ms);

		pwrok_signal_set(pwrok_signal, all_sys_pwrgd_in);
	}
}

enum power_state power_handle_state(enum power_state state)
{
	all_sys_pwrgd_pass_thru();

	common_intel_x86_handle_rsmrst(state);

	switch (state) {

	case POWER_G3S5:
		if (IS_ENABLED(CONFIG_CHIPSET_SLP_S3_L_OVERRIDE)) {
			/*
			 * Prevent glitches on the SLP_S3_L and PCH_PWROK
			 * signals while when the PP3300_A rail is turned on.
			 * Drive SLP_S3_L from the EC until PRIM rail is high.
			 */
			CPRINTS("Drive SLP_S3_L low during PP3300_A rampup");
			power_signal_disable_interrupt(SLP_S3_SIGNAL_L);
			gpio_set_flags(SLP_S3_SIGNAL_L, GPIO_ODR_LOW);
		}

		/* Default behavior - turn on PP5000 rail first */
		if (!IS_ENABLED(CONFIG_CHIPSET_PP3300_RAIL_FIRST))
			enable_pp5000_rail();

		/* Turn on the PP3300_PRIM rail. */
		GPIO_SET_LEVEL(GPIO_EN_PP3300_A, 1);

		if (power_wait_signals(IN_PGOOD_ALL_CORE))
			break;

		/* Turn on PP5000 after PP3300 enabled */
		if (IS_ENABLED(CONFIG_CHIPSET_PP3300_RAIL_FIRST))
			enable_pp5000_rail();

		break;

	default:
		break;
	}

	return common_intel_x86_power_handle_state(state);
}
