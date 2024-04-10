/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef CONFIG_ZTEST
#define CHARGER_SOLO 0
#endif

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
test_export_static void
baseboard_suspend_change(struct ap_power_ev_callback *cb,
			 struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_SUSPEND:
		/* Disable display backlight */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl),
				1);
		break;

	case AP_POWER_SHUTDOWN:
		/* Retimer disable */
		ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
		break;

	case AP_POWER_RESUME:
		/* Enable retimer and display backlight */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_disable_disp_bl),
				0);
		break;

	case AP_POWER_STARTUP:
		ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
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
		ccprints("Charger prochot asserted externally");
		hook_call_deferred(&check_charger_prochot_data, 0);
	} else
		ccprints("Charger prochot deasserted externally");
}

test_export_static void baseboard_init(void)
{
	static struct ap_power_ev_callback cb;
	const struct gpio_dt_spec *gpio_ec_sfh_int_h =
		GPIO_DT_FROM_NODELABEL(gpio_ec_sfh_int_h);
	const struct gpio_dt_spec *gpio_sfh_ec_int_h =
		GPIO_DT_FROM_NODELABEL(gpio_sfh_ec_int_h);

	/* Setup a suspend/resume callback */
	ap_power_ev_init_callback(&cb, baseboard_suspend_change,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN |
					  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&cb);
	/* Enable Power Group interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_groupc_s0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_lpddr_s0));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pg_lpddr_s3));

	/* Enable thermtrip interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_thermtrip));

	/* Enable prochot interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_prochot));
	throttle_ap_config_prochot(&prochot_cfg);

	/* Enable STB dumping interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_stb_dump));
	amd_stb_dump_init(gpio_ec_sfh_int_h, gpio_sfh_ec_int_h);
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_POST_I2C);

/**
 * b/227296844: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PCH_PWRBTN_L.  This can be as long as ~65ms after cold boot.  Then wait an
 * additional delay of T1a defined in the EDS before changing the power button.
 */
#define RSMRST_WAIT_DELAY 70
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
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_btn_l), level);
}

/* Note: signal parameter unused */
void baseboard_set_soc_pwr_pgood(enum gpio_signal unused)
{
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_pwr_good),
		gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s0_od)) &&
			gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_s0_pgood)));
}

/* TODO(b/248284045): Remove when boards switch to new chip */
#define MP2845A_I2C_ADDR_FLAGS 0x20
#define MP2854A_MFR_VOUT_CMPS_MAX_REG 0x69
#define MP2854A_MFR_LOW_PWR_SEL BIT(12)

__overridable bool board_supports_pcore_ocp(void)
{
	return true;
}

static void setup_mp2845(void)
{
	if (i2c_update16(chg_chips[CHARGER_SOLO].i2c_port,
			 MP2845A_I2C_ADDR_FLAGS, MP2854A_MFR_VOUT_CMPS_MAX_REG,
			 MP2854A_MFR_LOW_PWR_SEL, MASK_CLR))
		ccprints("Failed to send mp2845 workaround");

	if (board_supports_pcore_ocp())
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_soc_pcore_ocp));
}
DECLARE_DEFERRED(setup_mp2845);

void baseboard_s0_pgood(enum gpio_signal signal)
{
	baseboard_set_soc_pwr_pgood(signal);

	/* Chain off power signal interrupt handler for PG_PCORE_S0_R_OD */
	power_signal_interrupt(signal);

	/* Set up the MP2845, which is powered in S0 */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_s0_pgood)))
		hook_call_deferred(&setup_mp2845_data, 50 * MSEC);
	else
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_soc_pcore_ocp));
}

/* Note: signal parameter unused */
void baseboard_set_en_pwr_pcore(enum gpio_signal unused)
{
	/*
	 * EC must AND signals PG_LPDDR5_S3_OD, PG_GROUPC_S0_OD, and
	 * EN_PWR_S0_R
	 */
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_pcore_s0_r),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_lpddr5_s3_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_groupc_s0_od)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r)));

	/* Update EC_SOC_PWR_GOOD based on our results */
	baseboard_set_soc_pwr_pgood(unused);
}

void baseboard_en_pwr_s0(enum gpio_signal signal)
{
	/* EC must AND signals SLP_S3_L and PG_PWR_S5 */
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s0_r),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s3_l)) &&
			gpio_pin_get_dt(
				GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5)));

	/* Change EN_PWR_PCORE_S0_R if needed*/
	baseboard_set_en_pwr_pcore(signal);

	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}
#ifdef CONFIG_BOARD_USB_HUB_RESET
void baseboard_enable_hub(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_rst), 0);
}
DECLARE_DEFERRED(baseboard_enable_hub);
#endif /* CONFIG_BOARD_USB_HUB_RESET */

void baseboard_s5_pgood(enum gpio_signal signal)
{
#ifdef CONFIG_BOARD_USB_HUB_RESET
	/* We must enable the USB hub at least 30ms after S5 PGOOD */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pg_pwr_s5)))
		hook_call_deferred(&baseboard_enable_hub_data, 30 * MSEC);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_hub_rst), 1);
#endif /* CONFIG_BOARD_USB_HUB_RESET */

	/* Continue to our signal AND-ing and power interrupt */
	baseboard_en_pwr_s0(signal);
}

void baseboard_set_en_pwr_s3(enum gpio_signal signal)
{
	/* EC must enable PWR_S3 when SLP_S5_L goes high, disable on low */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pwr_s3),
			gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_slp_s5_l)));

	/* Chain off the normal power signal interrupt handler */
	power_signal_interrupt(signal);
}

void baseboard_soc_thermtrip(enum gpio_signal signal)
{
	ccprints("SoC thermtrip reported, shutting down");
	chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
}

void baseboard_soc_pcore_ocp(enum gpio_signal signal)
{
	ccprints("SoC Pcore OCP reported, shutting down");
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BOARD_CUSTOM);
}
