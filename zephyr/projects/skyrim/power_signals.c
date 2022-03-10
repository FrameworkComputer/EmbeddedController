/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "chipset.h"
#include "config.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ioexpander.h"
#include "power.h"
#include "timer.h"

/* Power Signal Input List */
/* TODO: b/218904113: Convert to using Zephyr GPIOs */
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

/* Chipset hooks */
static void baseboard_suspend_change(struct ap_power_ev_callback *cb,
				     struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_SUSPEND:
		/* Disable display backlight and retimer */
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl), 1);
		ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
		break;

	case AP_POWER_RESUME:
		/* Enable retimer and display backlight */
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl), 0);
		ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
		/* Any retimer tuning can be done after the retimer turns on */
		break;
	}
}

static void baseboard_init(void)
{
	static struct ap_power_ev_callback cb;

	/* Setup a suspend/resume callback */
	ap_power_ev_init_callback(&cb, baseboard_suspend_change,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);
	/* Enable Power Group interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_groupc_s0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_lpddr_s0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_lpddr_s3));
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_INIT_I2C + 1);

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

/* Note: signal parameter unused */
void baseboard_set_soc_pwr_pgood(enum gpio_signal unused)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_good),
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r)) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s0_od)));
}

/* Note: signal parameter unused */
void baseboard_set_en_pwr_pcore(enum gpio_signal unused)
{
	/*
	 * EC must AND signals PG_LPDDR5_S3_OD, PG_GROUPC_S0_OD, and
	 * EN_PWR_S0_R
	 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r),
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s3_od)) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od)) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r)));

	/* Update EC_SOC_PWR_GOOD based on our results */
	baseboard_set_soc_pwr_pgood(unused);
}

void baseboard_en_pwr_s0(enum gpio_signal signal)
{

	/* EC must AND signals SLP_S3_L and PG_PWR_S5 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r),
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5)));

	/* Change EN_PWR_PCORE_S0_R if needed*/
	baseboard_set_en_pwr_pcore(signal);

	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}

void baseboard_set_en_pwr_s3(enum gpio_signal signal)
{
	/* EC must enable PWR_S3 when SLP_S5_L goes high, disable on low */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s3),
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)));

	/* Chain off the normal power signal interrupt handler */
	power_signal_interrupt(signal);
}
