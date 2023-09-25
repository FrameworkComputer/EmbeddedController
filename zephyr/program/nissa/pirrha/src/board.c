/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "common.h"
#include "driver/charger/isl923x_public.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include <ap_power/ap_power.h>

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
	hook_call_deferred(&panel_power_change_deferred_data, 1 * MSEC);
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
	hook_call_deferred(&lcd_reset_change_deferred_data, 45 * MSEC);
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
	int enable;

	switch (data.event) {
	case AP_POWER_RESUME:
		/* USB A power */
		enable = 1;
		LOG_DBG("Enabling sub-board type-A power rails");
		if (isl923x_set_comparator_inversion(1, !!enable))
			LOG_WRN("Failed to %sable sub rails!",
				enable ? "en" : "dis");
		LOG_WRN("%sable sub rails!", enable ? "en" : "dis");

		/* ToDo: init mp2964 */
		break;
	case AP_POWER_SUSPEND:
	case AP_POWER_SHUTDOWN:
		enable = 0;
		LOG_DBG("Disabling sub-board type-A power rails");
		if (isl923x_set_comparator_inversion(1, !!enable))
			LOG_WRN("Failed to %sable sub rails!",
				enable ? "en" : "dis");
		LOG_WRN("%sable sub rails!", enable ? "en" : "dis");
		break;
	default:
		LOG_ERR("Unhandled usba power event %d", data.event);
		break;
	}
}

test_export_static void pirrha_callback_init(void)
{
	static struct ap_power_ev_callback pirrha_cb;

	ap_power_ev_init_callback(&pirrha_cb, power_handler,
				  AP_POWER_SHUTDOWN | AP_POWER_SUSPEND |
					  AP_POWER_RESUME);
	ap_power_ev_add_callback(&pirrha_cb);
}
DECLARE_HOOK(HOOK_INIT, pirrha_callback_init, HOOK_PRIO_DEFAULT);
