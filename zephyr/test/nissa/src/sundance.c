/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "extpower.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "nissa_sub_board.h"
#include "system.h"
#include "typec_control.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>

void reset_nct38xx_port(int port);

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, nissa_get_sb_type);
FAKE_VOID_FUNC(usb_charger_task_set_event, int, uint8_t);
FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VOID_FUNC(pd_send_host_event, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VOID_FUNC(nct38xx_reset_notify, int);
FAKE_VALUE_FUNC(int, extpower_is_present);
FAKE_VOID_FUNC(extpower_handle_update, int);
FAKE_VALUE_FUNC(int, ppc_set_vbus_source_current_limit, int,
		enum tcpc_rp_value);

unsigned int ppc_cnt = 2;

static void test_before(void *fixture)
{
	RESET_FAKE(nissa_get_sb_type);
	RESET_FAKE(usb_charger_task_set_event);
	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(pd_send_host_event);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(nct38xx_reset_notify);
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(extpower_handle_update);
	RESET_FAKE(ppc_set_vbus_source_current_limit);
}

ZTEST_SUITE(sundance, NULL, NULL, test_before, NULL, NULL);

ZTEST(sundance, test_board_check_extpower)
{
	extpower_is_present_fake.return_val = 1;
	board_check_extpower();
	zassert_equal(extpower_is_present_fake.call_count, 1);
	zassert_equal(extpower_handle_update_fake.call_count, 1);
	board_check_extpower();
	zassert_equal(extpower_is_present_fake.call_count, 2);
	zassert_equal(extpower_handle_update_fake.call_count, 1);
	extpower_is_present_fake.return_val = 0;
	board_check_extpower();
	zassert_equal(extpower_is_present_fake.call_count, 3);
	zassert_equal(extpower_handle_update_fake.call_count, 2);
}

ZTEST(sundance, test_is_sourcing_vbus)
{
	board_is_sourcing_vbus(0);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);
	board_is_sourcing_vbus(1);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 2);
}

ZTEST(sundance, test_reset_nct38xx_port_invalid_port)
{
	reset_nct38xx_port(3);
	zassert_equal(nct38xx_reset_notify_fake.call_count, 0);
}

ZTEST(sundance, test_set_active_charge_port_none)
{
	/* Don't return error even disable failed */
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_equal(EC_SUCCESS,
		      board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(2, ppc_vbus_sink_enable_fake.call_count);
	/* C0 */
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg0_history[0]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[0]);
	/* C1 */
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg0_history[1]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[1]);
}

ZTEST(sundance, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(3), EC_ERROR_INVAL,
		      "port 3 doesn't exist, should return error");
}

ZTEST(sundance, test_set_active_charge_port_currently_sourcing)
{
	ppc_is_sourcing_vbus_fake.return_val = 1;
	/* Attempting to sink on a port that's sourcing is an error */
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
}

ZTEST(sundance, test_set_active_charge_port)
{
	/* We can successfully start sinking on a port */
	zassert_ok(board_set_active_charge_port(0));

	/* Requested charging stop initially */
	/* Sinking on the other port was disabled */
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg0_history[0]);
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg1_history[0]);
	/* Sinking was enabled on the new port */
	zassert_equal(0, ppc_vbus_sink_enable_fake.arg0_history[1]);
	zassert_equal(1, ppc_vbus_sink_enable_fake.arg1_history[1]);
}

ZTEST(sundance, test_set_active_charge_port_enable_fail)
{
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
}

ZTEST(sundance, test_pd_power_supply_reset)
{
	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;

	pd_power_supply_reset(0);

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 1);
	}

	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(sundance, test_pd_set_power_supply_ready)
{
	zassert_ok(pd_set_power_supply_ready(0));

	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_val, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 0);
	}

	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 1);

	zassert_equal(pd_send_host_event_fake.call_count, 1);
}

ZTEST(sundance, test_pd_set_power_supply_ready_enable_fail)
{
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
}

ZTEST(sundance, test_pd_set_power_supply_ready_disable_fail)
{
	ppc_vbus_source_enable_fake.return_val = 1;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
}

ZTEST(sundance, test_reset_pd_mcu)
{
	nissa_get_sb_type_fake.return_val = NISSA_SB_NONE;
	board_reset_pd_mcu();
	zassert_equal(nct38xx_reset_notify_fake.call_count, 1);
	zassert_equal(nct38xx_reset_notify_fake.arg0_val, 0);

	nissa_get_sb_type_fake.return_val = NISSA_SB_C_A;
	board_reset_pd_mcu();
	zassert_equal(nct38xx_reset_notify_fake.call_count, 2);
	zassert_equal(nct38xx_reset_notify_fake.arg0_val, 0);
}

ZTEST(sundance, test_led)
{
	led_set_color_battery(EC_LED_COLOR_AMBER);
	/* led pin is low active, status 0 is turn on */
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl)),
		      "LED_1 is not on");
	led_set_color_battery(EC_LED_COLOR_WHITE);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl)),
		      "LED_2 is not on");
	/*
	 * Case for led pin is untestable because emulated GPIOs don't
	 * allow getting the current value of output pins.
	 */
}

ZTEST(sundance, test_led_brightness_range)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { 0 };

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl)),
		      "LED_1 is on");
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl)),
		      "LED_2 is on");

	led_get_brightness_range(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_AMBER], 1);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 1);

	brightness[EC_LED_COLOR_WHITE] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl)),
		      "LED_2 is not on");

	brightness[EC_LED_COLOR_AMBER] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl)),
		      "LED_1 is not on");

	brightness[EC_LED_COLOR_AMBER] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl)),
		      "LED_1 is on");
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl)),
		      "LED_2 is not on");

	brightness[EC_LED_COLOR_WHITE] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl)),
		      "LED_1 is not on");
	zassert_false(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl)),
		      "LED_2 is on");
}

ZTEST(sundance, test_typec_set_source_current_limit)
{
	typec_set_source_current_limit(0, TYPEC_RP_1A5);

	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 1);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg0_val, 0);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg1_val,
		      TYPEC_RP_1A5);

	typec_set_source_current_limit(1, TYPEC_RP_1A5);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 2);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg0_val, 1);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg1_val,
		      TYPEC_RP_1A5);
}
