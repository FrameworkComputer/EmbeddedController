/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "common.h"
#include "driver/charger/isl923x_public.h"
#include "driver/mp2964.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include <ap_power/ap_power.h>

/* MP2964 registers and values */
const static struct mp2964_reg_val mp2964_page0[] = {
	{ 0x28, 0x000C }, { 0x29, 0x0001 }, { 0x2C, 0x031F }, { 0x32, 0x1DFA },
	{ 0x38, 0x0035 }, { 0x3C, 0x00D1 }, { 0x40, 0x034D }, { 0x41, 0x0153 },
	{ 0x42, 0x014D }, { 0x44, 0x0053 }, { 0x45, 0x0053 }, { 0x46, 0x00D0 },
	{ 0x4D, 0xE13F }, { 0x53, 0x0025 }, { 0x60, 0x2DB0 }, { 0x62, 0x0CAD },
	{ 0xBD, 0x0019 }, { 0xD2, 0x00D0 }, { 0xD4, 0x0063 }, { 0xD6, 0x003F },
	{ 0xD8, 0x002D }, { 0xE0, 0x0012 }, { 0xE2, 0x00D0 }, { 0xE8, 0x00B7 },
	{ 0xE9, 0x00B7 }, { 0xEA, 0x00B7 }, { 0xEB, 0x00B7 }, { 0xEF, 0x00C7 },
	{ 0xF0, 0x00C7 }
};

const static struct mp2964_reg_val mp2964_page1[] = {
	{ 0x28, 0x000C }, { 0x29, 0x0001 }, { 0x2C, 0x02F7 }, { 0x32, 0x1DFA },
	{ 0x38, 0x002D }, { 0x3C, 0x00D1 }, { 0x40, 0x034D }, { 0x41, 0x0153 },
	{ 0x42, 0x014D }, { 0x44, 0x0053 }, { 0x45, 0x0053 }, { 0x46, 0x00D0 },
	{ 0x4D, 0xE13F }, { 0x53, 0x001D }, { 0x60, 0x32B0 }, { 0x62, 0x0CAA }
};

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

static const struct i2c_dt_spec lcdctrl =
	I2C_DT_SPEC_GET(DT_NODELABEL(lcdctrl));

/**
 * Enable panel power detection
 */
test_export_static void panel_power_detect_init(void)
{
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_pannel_power_change));
}
DECLARE_HOOK(HOOK_INIT, panel_power_detect_init, HOOK_PRIO_DEFAULT);

/**
 * Handle VPN / VSN for mipi display.
 */
test_export_static void panel_power_change_deferred(void)
{
	int signal = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp1800_panel_x));

	if (signal != 0) {
		i2c_reg_write_byte_dt(&lcdctrl, ISL98607_REG_VBST_OUT,
				      ISL98607_VBST_OUT_5P65);

		i2c_reg_write_byte_dt(&lcdctrl, ISL98607_REG_VN_OUT,
				      ISL98607_VN_OUT_5P5);

		i2c_reg_write_byte_dt(&lcdctrl, ISL98607_REG_VP_OUT,
				      ISL98607_VP_OUT_5P5);
	}

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tsp_ta),
			signal & extpower_is_present());
}
DECLARE_DEFERRED(panel_power_change_deferred);

void panel_power_change_interrupt(enum gpio_signal signal)
{
	/* Reset lid debounce time */
	hook_call_deferred(&panel_power_change_deferred_data,
			   1 * USEC_PER_MSEC);
}

/*
 * Detect LCD reset & control LCD DCDC power
 */
test_export_static void lcd_reset_detect_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lcd_rst_n));
}
DECLARE_HOOK(HOOK_INIT, lcd_reset_detect_init, HOOK_PRIO_DEFAULT);
/*
 * Handle VSP / VSN for mipi display when lcd turns off
 */
test_export_static void lcd_reset_change_deferred(void)
{
	int signal = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_lcd_rst_n));

	if (signal != 0)
		return;

	signal = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp1800_panel_x));

	if (signal == 0)
		return;

	i2c_reg_write_byte_dt(&lcdctrl, ISL98607_REG_ENABLE,
			      ISL97607_VP_VN_VBST_DIS);
}
DECLARE_DEFERRED(lcd_reset_change_deferred);
void lcd_reset_change_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&lcd_reset_change_deferred_data, 45 * USEC_PER_MSEC);
}

/**
 * Handle TSP_TA according to AC status
 */
test_export_static void handle_tsp_ta(void)
{
	int signal = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp1800_panel_x));

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tsp_ta),
			signal & extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, handle_tsp_ta, HOOK_PRIO_DEFAULT);

static void power_handler(struct ap_power_ev_callback *cb,
			  struct ap_power_ev_data data)
{
	int ret;
	static int chip_updated;

	switch (data.event) {
	case AP_POWER_STARTUP:
		if (chip_updated == 0) {
#ifdef CONFIG_PLATFORM_EC_BRINGUP
			CPRINTS("[mp2964] Attempting to tune mp2964");
#endif /* CONFIG_PLATFORM_EC_BRINGUP */
			ret = mp2964_tune(mp2964_page0,
					  ARRAY_SIZE(mp2964_page0),
					  mp2964_page1,
					  ARRAY_SIZE(mp2964_page1));

			if (ret == EC_SUCCESS) {
				chip_updated = 1;
#ifdef CONFIG_PLATFORM_EC_BRINGUP
				CPRINTS("[mp2964] Success to tune mp2964");
#endif /* CONFIG_PLATFORM_EC_BRINGUP */
			}
#ifdef CONFIG_PLATFORM_EC_BRINGUP
			else
				CPRINTS("[mp2964] Failed to tune mp2964");
#endif /* CONFIG_PLATFORM_EC_BRINGUP */
		}
		break;
	default:
		LOG_ERR("Unhandled power event %d", data.event);
		break;
	}
}

test_export_static void meliks_callback_init(void)
{
	static struct ap_power_ev_callback meliks_cb;

	ap_power_ev_init_callback(&meliks_cb, power_handler, AP_POWER_STARTUP);
	ap_power_ev_add_callback(&meliks_cb);
}
DECLARE_HOOK(HOOK_INIT, meliks_callback_init, HOOK_PRIO_DEFAULT);
