/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "nissa_hdmi.h"
#include "pujjoga.h"
#include "pujjoga_sub_board.h"
#include "system.h"
#include "typec_control.h"
#include "usb_charge.h"
#include "usb_pd.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

#define ASSERT_GPIO_FLAGS(spec, expected)                                  \
	do {                                                               \
		gpio_flags_t flags;                                        \
		zassert_ok(gpio_emul_flags_get((spec)->port, (spec)->pin,  \
					       &flags));                   \
		zassert_equal(flags, expected,                             \
			      "actual value was %#x; expected %#x", flags, \
			      expected);                                   \
	} while (0)

void reset_nct38xx_port(int port);
void pen_detect_change(struct ap_power_ev_callback *cb,
		       struct ap_power_ev_data data);

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

void hdmi_power_handler(struct ap_power_ev_callback *cb,
			struct ap_power_ev_data data);
void pujjoga_configure_hdmi_vcc(void);

int init_gpios(const struct device *unused);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(usb_charger_task_set_event, int, uint8_t);
FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VOID_FUNC(pd_send_host_event, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VOID_FUNC(nct38xx_reset_notify, int);
FAKE_VALUE_FUNC(int, extpower_is_present);
FAKE_VOID_FUNC(extpower_handle_update, int);

int ppc_cnt = 2;

static void test_before(void *fixture)
{
	RESET_FAKE(usb_charger_task_set_event);
	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(pd_send_host_event);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(nct38xx_reset_notify);
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(extpower_handle_update);
	RESET_FAKE(cros_cbi_get_fw_config);
}

static int gpio_emul_output_get_dt(const struct gpio_dt_spec *dt)
{
	return gpio_emul_output_get(dt->port, dt->pin);
}

static int gpio_emul_input_set_dt(const struct gpio_dt_spec *dt, int value)
{
	return gpio_emul_input_set(dt->port, dt->pin, value);
}

ZTEST_SUITE(pujjoga, NULL, NULL, test_before, NULL, NULL);

ZTEST(pujjoga, test_hdmi_power)
{
	const struct gpio_dt_spec *hdmi_vcc =
		GPIO_DT_FROM_NODELABEL(gpio_ec_hdmi_pwr);
	const struct gpio_dt_spec *vbus_rail =
		GPIO_DT_FROM_ALIAS(gpio_en_usb_a1_vbus);
	struct ap_power_ev_data data;

	nissa_configure_hdmi_power_gpios();
	pujjoga_configure_hdmi_vcc();
	zassert_equal(gpio_emul_output_get_dt(hdmi_vcc), 0);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	data.event = AP_POWER_STARTUP;
	hdmi_power_handler(NULL, data);
	zassert_equal(gpio_emul_output_get_dt(hdmi_vcc), 1);
	zassert_equal(gpio_emul_output_get_dt(vbus_rail), 1);
	data.event = AP_POWER_SHUTDOWN;
	hdmi_power_handler(NULL, data);
	zassert_equal(gpio_emul_output_get_dt(hdmi_vcc), 0);
	zassert_equal(gpio_emul_output_get_dt(vbus_rail), 0);
}

ZTEST(pujjoga, test_board_check_extpower)
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

ZTEST(pujjoga, test_is_sourcing_vbus)
{
	board_is_sourcing_vbus(0);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);
	board_is_sourcing_vbus(1);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 2);
}

ZTEST(pujjoga, test_reset_nct38xx_port_invalid_port)
{
	reset_nct38xx_port(3);
	zassert_equal(nct38xx_reset_notify_fake.call_count, 0);
}

ZTEST(pujjoga, test_set_active_charge_port_none)
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

ZTEST(pujjoga, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(3), EC_ERROR_INVAL,
		      "port 3 doesn't exist, should return error");
}

ZTEST(pujjoga, test_set_active_charge_port_currently_sourcing)
{
	ppc_is_sourcing_vbus_fake.return_val = 1;
	/* Attempting to sink on a port that's sourcing is an error */
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
}

ZTEST(pujjoga, test_set_active_charge_port)
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

ZTEST(pujjoga, test_set_active_charge_port_enable_fail)
{
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
}

ZTEST(pujjoga, test_pd_power_supply_reset)
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

ZTEST(pujjoga, test_pd_set_power_supply_ready)
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

ZTEST(pujjoga, test_pd_set_power_supply_ready_enable_fail)
{
	ppc_vbus_sink_enable_fake.return_val = 1;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
}

ZTEST(pujjoga, test_pd_set_power_supply_ready_disable_fail)
{
	ppc_vbus_source_enable_fake.return_val = 1;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
}

ZTEST(pujjoga, test_reset_pd_mcu)
{
	board_reset_pd_mcu();
	zassert_equal(nct38xx_reset_notify_fake.call_count, 1);
	zassert_equal(nct38xx_reset_notify_fake.arg0_val, 0);
}

ZTEST(pujjoga, test_led)
{
	const struct gpio_dt_spec *const led_1_odl =
		GPIO_DT_FROM_NODELABEL(gpio_led_1_odl);
	const struct gpio_dt_spec *const led_2_odl =
		GPIO_DT_FROM_NODELABEL(gpio_led_2_odl);
	const struct gpio_dt_spec *const power_led =
		GPIO_DT_FROM_NODELABEL(gpio_power_led);

	/* led pin is low active, status 0 is turn on */
	led_set_color_battery(EC_LED_COLOR_AMBER);
	zassert_false(gpio_emul_output_get_dt(led_1_odl), "LED_1 is on");
	zassert_false(gpio_emul_output_get_dt(led_2_odl), "LED_2 is on");
	led_set_color_battery(EC_LED_COLOR_RED);
	zassert_false(gpio_emul_output_get_dt(led_1_odl), "LED_1 is on");
	zassert_true(gpio_emul_output_get_dt(led_2_odl), "LED_2 is off");
	led_set_color_battery(EC_LED_COLOR_GREEN);
	zassert_true(gpio_emul_output_get_dt(led_1_odl), "LED_1 is off");
	zassert_false(gpio_emul_output_get_dt(led_2_odl), "LED_2 is on");
	led_set_color_power(EC_LED_COLOR_WHITE);
	zassert_false(gpio_emul_output_get_dt(power_led), "PWR_LED is on");
	/*
	 * Case for led pin is untestable because emulated GPIOs don't
	 * allow getting the current value of output pins.
	 */
}

ZTEST(pujjoga, test_led_brightness_range)
{
	const struct gpio_dt_spec *const led_1_odl =
		GPIO_DT_FROM_NODELABEL(gpio_led_1_odl);
	const struct gpio_dt_spec *const led_2_odl =
		GPIO_DT_FROM_NODELABEL(gpio_led_2_odl);
	const struct gpio_dt_spec *const power_led =
		GPIO_DT_FROM_NODELABEL(gpio_power_led);
	uint8_t brightness[EC_LED_COLOR_COUNT] = { 0 };

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_true(gpio_emul_output_get_dt(led_1_odl), "LED_1 is off");
	zassert_true(gpio_emul_output_get_dt(led_2_odl), "LED_2 is off");

	/* Verify LED colors defined in device tree are reflected in the
	 * brightness array.
	 */
	led_get_brightness_range(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_RED], 1);
	zassert_equal(brightness[EC_LED_COLOR_GREEN], 1);
	zassert_equal(brightness[EC_LED_COLOR_AMBER], 1);

	memset(brightness, 0, sizeof(brightness));

	led_get_brightness_range(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 1);

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_GREEN] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);

	zassert_true(gpio_emul_output_get_dt(led_1_odl), "LED_1 is off");
	zassert_false(gpio_emul_output_get_dt(led_2_odl), "LED_2 is on");

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_RED] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);

	zassert_false(gpio_emul_output_get_dt(led_1_odl), "LED_1 is on");
	zassert_true(gpio_emul_output_get_dt(led_2_odl), "LED_2 is off");

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_AMBER] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);

	zassert_false(gpio_emul_output_get_dt(led_1_odl), "LED_1 is on");
	zassert_false(gpio_emul_output_get_dt(led_2_odl), "LED_2 is on");

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_WHITE] = 1;
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);

	zassert_false(gpio_pin_get_dt(power_led), "PWR_LED is on");

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_BLUE] = 1;
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_true(gpio_emul_output_get_dt(power_led), "PWR_LED is off");
}

enum pujjoga_sub_board_type pujjoga_get_sb_type(void);

static uint32_t fw_config_value;

/* Set the value of the CBI fw_config field returned by the fake. */
static void set_fw_config_value(uint32_t value)
{
	fw_config_value = value;
}

static int
get_fake_sub_board_fw_config_field(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

ZTEST(pujjoga, test_db_with_a_and_hdmi)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;

	/* Reset cached global state. */
	pujjoga_cached_sub_board = PUJJOGA_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_3);
	zassert_equal(pujjoga_get_sb_type(), PUJJOGA_SB_HDMI_A,
		      "SB: HDMI, USB type A");

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* USB-A controls are enabled. */
	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_en_sub_usb_a1_vbus),
			  GPIO_OUTPUT); /* A1 VBUS enable */
}

ZTEST(pujjoga, test_unset_board)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;

	/* Reset cached global state. */
	pujjoga_cached_sub_board = PUJJOGA_SB_UNKNOWN;
	fw_config_value = -1;

	/* fw_config with an unset sub-board means none is present. */
	set_fw_config_value(0);
	zassert_equal(pujjoga_get_sb_type(), PUJJOGA_SB_NONE, "SB: None");

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* USB-A controls are disabled. */
	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_en_sub_usb_a1_vbus),
			  GPIO_DISCONNECTED); /* A1 VBUS disable */
}

static int get_fw_config_error(enum cbi_fw_config_field_id field,
			       uint32_t *value)
{
	return EC_ERROR_UNKNOWN;
}

ZTEST(pujjoga, test_cbi_error)
{
	/*
	 * Reading fw_config from CBI returns an error, so sub-board is treated
	 * as absent.
	 */
	cros_cbi_get_fw_config_fake.custom_fake = get_fw_config_error;
	zassert_equal(pujjoga_get_sb_type(), PUJJOGA_SB_NONE, "SB: None");
}

ZTEST(pujjoga, test_pen_detect_interrupt)
{
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen);
	const struct gpio_dt_spec *const pen_irq =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);

	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pen_det_l));
	gpio_emul_input_set(pen_irq->port, pen_irq->pin, 0);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 1);

	/*  De-assert the IRQ */
	gpio_emul_input_set(pen_irq->port, pen_irq->pin, 1);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 0);
}

ZTEST(pujjoga, test_pen_power_control)
{
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen);
	const struct gpio_int_config *const pen_detect_int =
		GPIO_INT_FROM_NODELABEL(int_pen_det_l);
	struct ap_power_ev_data data;

	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set_dt(pen_detect_gpio, 1), NULL);

	data.event = AP_POWER_STARTUP;
	pen_detect_change(NULL, data);
	/* gpio_en_pp5000_pen_x become high */
	gpio_enable_dt_interrupt(pen_detect_int);
	zassert_ok(gpio_emul_input_set_dt(pen_detect_gpio, 0), NULL);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 1);

	/* gpio_en_pp5000_pen_x keep low if AP_POWER_SHUTDOWN */
	data.event = AP_POWER_SHUTDOWN;
	pen_detect_change(NULL, data);
	gpio_disable_dt_interrupt(pen_detect_int);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 0);
}
