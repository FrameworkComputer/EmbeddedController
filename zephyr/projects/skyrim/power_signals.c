/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "config.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "timer.h"

/* Power Signal Input List */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

static void baseboard_interrupt_init(void)
{
	/* Enable Power Group interrupts. */
	gpio_enable_interrupt(GPIO_PG_GROUPC_S0_OD);
	gpio_enable_interrupt(GPIO_PG_LPDDR5_S0_OD);
	gpio_enable_interrupt(GPIO_PG_LPDDR5_S3_OD);
}
DECLARE_HOOK(HOOK_INIT, baseboard_interrupt_init, HOOK_PRIO_INIT_I2C + 1);

/**
 * b/175324615: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PCH_PWRBTN_L.
 */
void board_pwrbtn_to_pch(int level)
{
	timestamp_t start;
	const uint32_t timeout_rsmrst_rise_us = 30 * MSEC;

	/* Add delay for G3 exit if asserting PWRBTN_L and RSMRST_L is low. */
	if (!level && !gpio_get_level(GPIO_PCH_RSMRST_L)) {
		start = get_time();
		do {
			usleep(200);
			if (gpio_get_level(GPIO_PCH_RSMRST_L))
				break;
		} while (time_since32(start) < timeout_rsmrst_rise_us);

		if (!gpio_get_level(GPIO_PCH_RSMRST_L))
			ccprints("Error pwrbtn: RSMRST_L still low");

		msleep(16);
	}
	gpio_set_level(GPIO_PCH_PWRBTN_L, level);
}

/* Note: signal parameter unused */
void baseboard_set_soc_pwr_pgood(enum gpio_signal unused)
{
	gpio_set_level(GPIO_EC_SOC_PWR_GOOD,
				gpio_get_level(GPIO_EN_PWR_PCORE_S0_R) &&
				gpio_get_level(GPIO_PG_LPDDR5_S0_OD));
}

/* Note: signal parameter unused */
void baseboard_set_en_pwr_pcore(enum gpio_signal unused)
{
	/*
	 * EC must AND signals PG_LPDDR5_S3_OD, PG_GROUPC_S0_OD, and
	 * EN_PWR_S0_R
	 */
	gpio_set_level(GPIO_EN_PWR_PCORE_S0_R,
					gpio_get_level(GPIO_PG_LPDDR5_S3_OD) &&
					gpio_get_level(GPIO_PG_GROUPC_S0_OD) &&
					gpio_get_level(GPIO_EN_PWR_S0_R));

	/* Update EC_SOC_PWR_GOOD based on our results */
	baseboard_set_soc_pwr_pgood(unused);
}

void baseboard_en_pwr_s0(enum gpio_signal signal)
{

	/* EC must AND signals SLP_S3_L and PG_PWR_S5 */
	gpio_set_level(GPIO_EN_PWR_S0_R,
		       gpio_get_level(GPIO_PCH_SLP_S3_L) &&
		       gpio_get_level(GPIO_S5_PGOOD));

	/* Change EN_PWR_PCORE_S0_R if needed*/
	baseboard_set_en_pwr_pcore(signal);

	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}
