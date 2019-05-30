/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Apollolake chipset power control module for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "intel_x86.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/*
 * force_shutdown is used to maintain chipset shutdown request. This request
 * needs to be handled from within the chipset task.
 */
static int force_shutdown;

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_POWER_S0IX
	[X86_SLP_S0_N] = {
		GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED",
	},
#endif
	[X86_SLP_S3_N] = {
		GPIO_PCH_SLP_S3_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_N] = {
		GPIO_PCH_SLP_S4_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S4_DEASSERTED",
	},
	[X86_SUSPWRDNACK] = {
		GPIO_SUSPWRDNACK,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SUSPWRDNACK_DEASSERTED",
	},
	[X86_ALL_SYS_PG] = {
		GPIO_ALL_SYS_PGOOD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"ALL_SYS_PGOOD",
	},
	[X86_RSMRST_N] = {
		GPIO_RSMRST_L_PGOOD,
		POWER_SIGNAL_ACTIVE_HIGH,
		"RSMRST_L",
	},
	[X86_PGOOD_PP3300] = {
		GPIO_PP3300_PG,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP3300_PG",
	},
	[X86_PGOOD_PP5000] = {
		GPIO_PP5000_PG,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PP5000_PG",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

__attribute__((weak)) void chipset_do_shutdown(void)
{
	/* Need to implement board specific shutdown */
}

static void internal_chipset_shutdown(void)
{
	/*
	 * UART buffer gets overwritten by other tasks if it is not explicitly
	 * flushed before printing it on the console by same task. Hence, clean
	 * up the UART buffer so that all the debug messages are printed on the
	 * UART console before doing shutdown.
	 */
	cflush();

	CPRINTS("%s()", __func__);

	force_shutdown = 0;
	chipset_do_shutdown();
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s: %d", __func__, reason);
	report_ap_reset(reason);

	/*
	 * This function is called from multiple tasks and hence it is racy! But
	 * since things are going down hard, it does not matter if some task
	 * misses out.
	 */
	force_shutdown = 1;
	task_wake(TASK_ID_CHIPSET);
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

void chipset_handle_espi_reset_assert(void)
{
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
		internal_chipset_shutdown();

		new_state = POWER_S5G3;
		goto rsmrst_handle;

	}

	/* If force shutdown is requested, perform that. */
	if (force_shutdown)
		internal_chipset_shutdown();

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
