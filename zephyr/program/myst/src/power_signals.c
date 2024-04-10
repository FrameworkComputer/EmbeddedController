/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "charger.h"
#include "chipset.h"
#include "config.h"
#include "driver/amd_stb.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "power.h"
#include "power/amd_x86.h"
#include "throttle_ap.h"
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
/* looks like we don't have this on Myst.. but POWER_SIGNAL_COUNT is needed */
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

static void handle_prochot(bool asserted, void *data);

const struct prochot_cfg prochot_cfg = {
	.gpio_prochot_in = GPIO_CPU_PROCHOT,
	.callback = handle_prochot,
};

/* Chipset hooks */
static void baseboard_suspend_change(struct ap_power_ev_callback *cb,
				     struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_SUSPEND:
		/* Disable display backlight and retimer */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl),
				1);
		break;

	case AP_POWER_RESUME:
		/* Enable retimer and display backlight */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl),
				0);
		/* Any retimer tuning can be done after the retimer turns on */
		break;
	}
}

static void check_charger_prochot(void)
{
	print_charger_prochot(CHARGER_SOLO);
}
DECLARE_DEFERRED(check_charger_prochot);

static void handle_prochot(bool asserted, void *data)
{
	if (asserted) {
		ccprints("Prochot asserted externally");
		hook_call_deferred(&check_charger_prochot_data, 0);
	} else
		ccprints("Prochot deasserted externally");
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
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_lpddr_s3));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_vddq_mem_od));

	/* Enable prochot interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_prochot));
	throttle_ap_config_prochot(&prochot_cfg);
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_POST_I2C);

/**
 * b/275949288: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PCH_PWRBTN_L.  This can be as long as ~65ms after cold boot.  Then wait an
 * additional delay of T1a defined in the EDS before changing the power button.
 */
#define RSMRST_WAIT_DELAY 65
#define EDS_PWR_BTN_RSMRST_T1A_DELAY 16
void board_pwrbtn_to_pch(int level)
{
	timestamp_t start;

	/* Add delay for G3 exit if asserting PWRBTN_L and RSMRST_L is low. */
	if (!level &&
	    !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l))) {
		start = get_time();
		do {
			crec_usleep(500);
			if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
				    gpio_ec_soc_rsmrst_l)))
				break;
		} while (time_since32(start) < (RSMRST_WAIT_DELAY * MSEC));

		if (!gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_ec_soc_rsmrst_l)))
			ccprints("Error pwrbtn: RSMRST_L still low");

		crec_msleep(EDS_PWR_BTN_RSMRST_T1A_DELAY);
	}
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_btn_odl), level);
}

/* Note: signal parameter unused */
void baseboard_set_soc_pwr_pgood(enum gpio_signal unused)
{
	/*
	 * EC must AND signals PG_VDDQ_MEM_OD, PG_GROUPC_S0_OD, and
	 * EN_PWR_S0_R. AND PG_LPDDR5_S3_OD for good measure since it
	 * should be enabled in S0 anyway.
	 */
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_good),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_vddq_mem_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s3_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_pcore_s0_r_od)));
}

void baseboard_s0_pgood(enum gpio_signal signal)
{
	baseboard_set_soc_pwr_pgood(signal);

	/* Chain off power signal interrupt handler for PG_PCORE_S0_R_OD */
	power_signal_interrupt(signal);
}

/* Note: signal parameter unused */
void baseboard_set_en_pwr_pcore(enum gpio_signal unused)
{
	/*
	 * EC must AND signals PG_LPDDR5_S3_OD, PG_GROUPC_S0_OD, and
	 * EN_PWR_S0_R
	 */
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s3_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0)));

	/* Update EC_SOC_PWR_GOOD based on our results */
	baseboard_set_soc_pwr_pgood(unused);
}

void baseboard_en_pwr_s0(enum gpio_signal signal)
{
	/* EC must AND signals SLP_S3_L and PG_PWR_S5 */
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5)));

	/*
	 * Thermaltrip interrupt has a pull-up to the S0 domain, enable/disable
	 * so that we don't get spurious interrupts when S0 goes down.
	 */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0)))
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_soc_thermtrip));
	else
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_soc_thermtrip));

	/* Change EN_PWR_PCORE_S0_R if needed*/
	baseboard_set_en_pwr_pcore(signal);

	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}

void baseboard_s5_pgood(enum gpio_signal signal)
{
	/* Continue to our signal AND-ing and power interrupt */
	baseboard_en_pwr_s0(signal);
}

void baseboard_set_en_pwr_s3(enum gpio_signal signal)
{
	/* Chain off the normal power signal interrupt handler */
	power_signal_interrupt(signal);
}

void baseboard_soc_thermtrip(enum gpio_signal signal)
{
	ccprints("SoC thermtrip reported, shutting down");
	chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
}
