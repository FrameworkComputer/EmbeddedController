/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/gpio.h>

#include "chipset.h"
#include "config.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "timer.h"

/* Wake Sources */
/* TODO: b/218904113: Convert to using Zephyr GPIOs */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* Power Signal Input List */
/* TODO: b/218904113: Convert to using Zephyr GPIOs */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_N] = {
		.gpio = GPIO_PCH_SLP_S0_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S0_DEASSERTED",
	},
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

static int baseboard_interrupt_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	/* Enable Power Group interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_groupc_s0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_lpddr4x_s3));

	return 0;
}
SYS_INIT(baseboard_interrupt_init, APPLICATION, HOOK_PRIO_POST_I2C);

/**
 * b/175324615: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PCH_PWRBTN_L.
 */
void board_pwrbtn_to_pch(int level)
{
	timestamp_t start;
	const uint32_t timeout_rsmrst_rise_us = 30 * MSEC;

	/* Add delay for G3 exit if asserting PWRBTN_L and RSMRST_L is low. */
	if (!level &&
	    !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l))) {
		start = get_time();
		do {
			usleep(200);
			if (gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l)))
				break;
		} while (time_since32(start) < timeout_rsmrst_rise_us);

		if (!gpio_pin_get_dt(
		    GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l)))
			ccprints("Error pwrbtn: RSMRST_L still low");

		msleep(16);
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_btn_l), level);
}

void baseboard_en_pwr_pcore_s0(enum gpio_signal signal)
{

	/* EC must AND signals PG_LPDDR4X_S3_OD and PG_GROUPC_S0_OD */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r),
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr4x_s3_od)) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od)));
}

void baseboard_en_pwr_s0(enum gpio_signal signal)
{

	/* EC must AND signals SLP_S3_L and PG_PWR_S5 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r),
		       gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) &&
		       gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5)));

	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}

void baseboard_set_en_pwr_s3(enum gpio_signal signal)
{
	/* EC has no EN_PWR_S3 on this board */

	/* Chain off the normal power signal interrupt handler */
	power_signal_interrupt(signal);
}
