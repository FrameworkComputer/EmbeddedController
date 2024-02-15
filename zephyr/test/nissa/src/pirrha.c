/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "charge_manager.h"
#include "charger_profile_override.h"
#include "driver/charger/isl923x_public.h"
#include "driver/tcpm/raa489000.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "mock/isl923x.h"
#include "pirrha.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "typec_control.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

void reduce_input_voltage_when_full(void);

/* charging current is limited to 0.45C */
#define CHARGING_CURRENT_45C 2601

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpci_emul_0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpci_emul_1))

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);

FAKE_VALUE_FUNC(int, raa489000_enable_asgate, int, bool);
FAKE_VALUE_FUNC(int, raa489000_set_output_current, int, enum tcpc_rp_value);
FAKE_VOID_FUNC(raa489000_hibernate, int, bool);
FAKE_VALUE_FUNC(enum ec_error_list, raa489000_is_acok, int, bool *);
FAKE_VOID_FUNC(extpower_handle_update, int);
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port);
FAKE_VALUE_FUNC(enum ec_error_list, charger_discharge_on_ac, int);
FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VOID_FUNC(usb_charger_task_set_event_sync, int, uint8_t);
FAKE_VALUE_FUNC(int, charge_get_percent);
FAKE_VALUE_FUNC(int, isl923x_set_comparator_inversion, int, int);

static void test_before(void *fixture)
{
	RESET_FAKE(cbi_get_board_version);

	RESET_FAKE(raa489000_enable_asgate);
	RESET_FAKE(raa489000_set_output_current);
	RESET_FAKE(raa489000_hibernate);
	RESET_FAKE(raa489000_is_acok);
	RESET_FAKE(extpower_handle_update);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(usb_charger_task_set_event_sync);
	RESET_FAKE(charge_get_percent);
	RESET_FAKE(isl923x_set_comparator_inversion);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_SUITE(pirrha, NULL, NULL, test_before, NULL, NULL);

ZTEST(pirrha, test_charger_hibernate)
{
	/* board_hibernate() asks the chargers to hibernate. */
	board_hibernate();

	zassert_equal(raa489000_hibernate_fake.call_count, 2);
	zassert_equal(raa489000_hibernate_fake.arg0_history[0],
		      CHARGER_SECONDARY);
	zassert_true(raa489000_hibernate_fake.arg1_history[0]);
	zassert_equal(raa489000_hibernate_fake.arg0_history[1],
		      CHARGER_PRIMARY);
	zassert_true(raa489000_hibernate_fake.arg1_history[1]);
}

ZTEST(pirrha, test_check_extpower)
{
	/* Ensure initial state is no expower present */
	board_check_extpower();
	RESET_FAKE(extpower_handle_update);

	/* Update with no change does nothing. */
	board_check_extpower();
	zassert_equal(extpower_handle_update_fake.call_count, 0);

	/* Becoming present updates */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_present;
	board_check_extpower();
	zassert_equal(extpower_handle_update_fake.call_count, 1);
	zassert_equal(extpower_handle_update_fake.arg0_val, 1);

	/* Errors are treated as not plugged in */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_error;
	board_check_extpower();
	zassert_equal(extpower_handle_update_fake.call_count, 2);
	zassert_equal(extpower_handle_update_fake.arg0_val, 0);
}

ZTEST(pirrha, test_is_sourcing_vbus)
{
	tcpci_emul_set_reg(TCPC0, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS |
				   TCPC_REG_POWER_STATUS_VBUS_PRES);
	zassert_true(board_is_sourcing_vbus(0));

	tcpci_emul_set_reg(TCPC1, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SINKING_VBUS |
				   TCPC_REG_POWER_STATUS_VBUS_PRES);
	zassert_false(board_is_sourcing_vbus(1));
}

ZTEST(pirrha, test_set_active_charge_port_none)
{
	uint16_t reg;

	/* Setting CHARGE_PORT_NONE disables sinking on all ports */
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 2);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[0], 0);
	zassert_equal(raa489000_enable_asgate_fake.arg1_history[0], false);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[1], 1);
	zassert_equal(raa489000_enable_asgate_fake.arg1_history[1], false);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_LOW);
	tcpci_emul_get_reg(TCPC1, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_LOW);
}

ZTEST(pirrha, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(4), EC_ERROR_INVAL,
		      "port 4 doesn't exist, should return error");
}

ZTEST(pirrha, test_set_active_charge_port_currently_sourcing)
{
	/* Attempting to sink on a port that's sourcing is an error */
	tcpci_emul_set_reg(TCPC1, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
}

ZTEST(pirrha, test_set_active_charge_port)
{
	uint16_t reg;

	/* We can successfully start sinking on a port */
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 2);
	zassert_equal(charger_discharge_on_ac_fake.call_count, 2);

	/* Requested charging stop initially */
	zassert_equal(charger_discharge_on_ac_fake.arg0_history[0], 1);
	/* Sinking on the other port was disabled */
	tcpci_emul_get_reg(TCPC1, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_LOW);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[0], 1);
	zassert_equal(raa489000_enable_asgate_fake.arg1_history[0], false);
	/* Sinking was enabled on the new port */
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_HIGH);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[1], 0);
	zassert_equal(raa489000_enable_asgate_fake.arg1_history[1], true);
	/* Resumed charging */
	zassert_equal(charger_discharge_on_ac_fake.arg0_history[1], 0);
}

ZTEST(pirrha, test_set_active_charge_port_enable_fail)
{
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);

	/* Charging was enabled again after the error */
	zassert_equal(charger_discharge_on_ac_fake.arg0_val, 0);
}

ZTEST(pirrha, test_set_active_charge_port_disable_fail)
{
	/* Failing to disable sinking on the other port isn't fatal */
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		TCPC_REG_COMMAND);
	zassert_ok(board_set_active_charge_port(0));
}

ZTEST(pirrha, test_tcpc_get_alert_status)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);

	/* Sub-board IO configuration is handled by other inits */
	gpio_pin_configure_dt(c1_int, GPIO_INPUT_PULL_UP);

	/* Both IRQs are asserted */
	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	gpio_emul_input_set(c1_int->port, c1_int->pin, 0);

	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 1);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_0);

	/* Bit 14 is ignored */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 0x4000);
	zassert_equal(tcpc_get_alert_status(), 0);

	/* Port 1 works too */
	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT, 0x8000);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_1);
}

ZTEST(pirrha, test_pd_power_supply_reset)
{
	uint16_t reg;

	/* Stops any active sourcing on the given port */
	pd_power_supply_reset(0);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SRC_CTRL_LOW);
}

ZTEST(pirrha, test_set_source_current_limit)
{
	/* Args pass through raa489000_set_output_current() */
	typec_set_source_current_limit(0, TYPEC_RP_3A0);
	zassert_equal(raa489000_set_output_current_fake.call_count, 1);
	zassert_equal(raa489000_set_output_current_fake.arg0_val, 0);
	zassert_equal(raa489000_set_output_current_fake.arg1_val, TYPEC_RP_3A0);

	/* A port that doesn't exist does nothing */
	typec_set_source_current_limit(3, TYPEC_RP_USB);
	zassert_equal(raa489000_set_output_current_fake.call_count, 1);
}

static int chipset_in_state_break_tcpc_command(int state_mask)
{
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	return 0;
}

ZTEST(pirrha, test_pd_set_power_supply_ready)
{
	uint16_t reg;

	/* Initially sinking VBUS so we can see that gets disabled */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SINKING_VBUS);

	zassert_ok(pd_set_power_supply_ready(0));
	tcpci_emul_get_reg(TCPC0, TCPC_REG_POWER_STATUS, &reg);
	zassert_equal(reg, TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_equal(raa489000_enable_asgate_fake.call_count, 1);
	zassert_equal(raa489000_enable_asgate_fake.arg0_val, 0);
	zassert_equal(raa489000_enable_asgate_fake.arg1_val, true);

	/* Assorted errors are propagated: enable_asgate() fails */
	raa489000_enable_asgate_fake.return_val = EC_ERROR_UNIMPLEMENTED;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);

	/* Write to enable VBUS fails */
	chipset_in_state_fake.custom_fake = chipset_in_state_break_tcpc_command;
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	chipset_in_state_fake.custom_fake = NULL;

	/* Write to disable sinking fails */
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	zassert_not_equal(pd_set_power_supply_ready(0), EC_SUCCESS);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	/* AP is off */
	chipset_in_state_fake.return_val = 1;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_NOT_POWERED);

	/* Invalid port number requested */
	zassert_equal(pd_set_power_supply_ready(2), EC_ERROR_INVAL);
}

ZTEST(pirrha, test_reset_pd_mcu)
{
	/* Doesn't do anything */
	board_reset_pd_mcu();
}

ZTEST(pirrha, test_process_pd_alert)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);

	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	board_process_pd_alert(0);
	/* We ran BC1.2 processing inline */
	zassert_equal(usb_charger_task_set_event_sync_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.arg0_val, 0);
	zassert_equal(usb_charger_task_set_event_sync_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
	/*
	 * This should also call schedule_deferred_pd_interrupt() again, but
	 * there's no good way to verify that.
	 */

	/* Port 1 also works */
	gpio_emul_input_set(c1_int->port, c1_int->pin, 0);
	board_process_pd_alert(1);
	zassert_equal(usb_charger_task_set_event_sync_fake.call_count, 2);
	zassert_equal(usb_charger_task_set_event_sync_fake.arg0_val, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
}

ZTEST(pirrha, test_charger_profile_override)
{
	int rv;
	struct charge_state_data data;

	data.requested_current = CHARGING_CURRENT_45C + 1;
	chipset_in_state_fake.return_val = 8;
	rv = charger_profile_override(&data);
	zassert_ok(rv);
	zassert_equal(data.requested_current, CHARGING_CURRENT_45C);
}

ZTEST(pirrha, test_charger_profile_override_get_param)
{
	zassert_equal(charger_profile_override_get_param(0, NULL),
		      EC_RES_INVALID_PARAM);
}

ZTEST(pirrha, test_charger_profile_override_set_param)
{
	zassert_equal(charger_profile_override_set_param(0, 0),
		      EC_RES_INVALID_PARAM);
}

ZTEST(pirrha, test_reduce_input_voltage_when_full)
{
	chipset_in_state_fake.return_val = 4;
	charge_get_percent_fake.return_val = 100;
	reduce_input_voltage_when_full();

	charge_get_percent_fake.return_val = 99;
	reduce_input_voltage_when_full();
}

ZTEST(pirrha, test_panel_power_change)
{
	const struct gpio_dt_spec *panel_x =
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp1800_panel_x);
	const struct gpio_dt_spec *tsp_ta =
		GPIO_DT_FROM_NODELABEL(gpio_ec_tsp_ta);

	panel_power_detect_init();

	zassert_ok(gpio_emul_input_set(panel_x->port, panel_x->pin, 0));

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_present;
	zassert_ok(gpio_emul_input_set(panel_x->port, panel_x->pin, 1));
	k_sleep(K_MSEC(20));
	zassert_equal(gpio_emul_output_get(tsp_ta->port, tsp_ta->pin), 1);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;
	zassert_ok(gpio_emul_input_set(panel_x->port, panel_x->pin, 0));
	k_sleep(K_MSEC(20));
	zassert_equal(gpio_emul_output_get(tsp_ta->port, tsp_ta->pin), 0);
}

ZTEST(pirrha, test_lcd_reset_change)
{
	const struct gpio_dt_spec *lcd_rst_n =
		GPIO_DT_FROM_NODELABEL(gpio_lcd_rst_n);
	const struct gpio_dt_spec *panel_x =
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp1800_panel_x);

	lcd_reset_detect_init();

	zassert_ok(gpio_emul_input_set(panel_x->port, panel_x->pin, 1));
	zassert_ok(gpio_emul_input_set(lcd_rst_n->port, lcd_rst_n->pin, 1));
	k_sleep(K_MSEC(50));
	zassert_ok(gpio_emul_input_set(lcd_rst_n->port, lcd_rst_n->pin, 0));
	k_sleep(K_MSEC(50));
}

ZTEST(pirrha, test_handle_tsp_ta)
{
	const struct gpio_dt_spec *panel_x =
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp1800_panel_x);
	const struct gpio_dt_spec *tsp_ta =
		GPIO_DT_FROM_NODELABEL(gpio_ec_tsp_ta);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_present;
	zassert_ok(gpio_emul_input_set(panel_x->port, panel_x->pin, 1));
	handle_tsp_ta();
	zassert_equal(gpio_emul_output_get(tsp_ta->port, tsp_ta->pin), 1);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;
	zassert_ok(gpio_emul_input_set(panel_x->port, panel_x->pin, 0));
	handle_tsp_ta();
	zassert_equal(gpio_emul_output_get(tsp_ta->port, tsp_ta->pin), 0);
}

ZTEST(pirrha, test_pirrha_callback_init)
{
	pirrha_callback_init();

	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(isl923x_set_comparator_inversion_fake.call_count, 1);
	zassert_equal(isl923x_set_comparator_inversion_fake.arg0_val, 1);
	zassert_equal(isl923x_set_comparator_inversion_fake.arg1_val, 1);

	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(isl923x_set_comparator_inversion_fake.call_count, 2);
	zassert_equal(isl923x_set_comparator_inversion_fake.arg0_val, 1);
	zassert_equal(isl923x_set_comparator_inversion_fake.arg1_val, 0);
}

ZTEST(pirrha, test_led_set_color_power)
{
	const struct gpio_dt_spec *led_r =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_r);
	const struct gpio_dt_spec *led_g =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_g);
	const struct gpio_dt_spec *led_b =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_b);

	zassert_equal(1, led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED));
	zassert_equal(1, led_auto_control_is_enabled(EC_LED_ID_POWER_LED));
	led_set_color_power(EC_LED_COLOR_BLUE);
	led_set_color_power(EC_LED_COLOR_BLUE);
	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 1);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 1);
	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 0);

	led_set_color_power(EC_LED_COLOR_RED);
	led_set_color_power(EC_LED_COLOR_RED);
	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 1);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 1);
	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 1);
}

ZTEST(pirrha, test_led_set_color_battery)
{
	const struct gpio_dt_spec *led_r =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_r);
	const struct gpio_dt_spec *led_g =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_g);
	const struct gpio_dt_spec *led_b =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_b);

	zassert_equal(1, led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED));
	zassert_equal(1, led_auto_control_is_enabled(EC_LED_ID_POWER_LED));
	led_set_color_battery(EC_LED_COLOR_BLUE);
	led_set_color_battery(EC_LED_COLOR_BLUE);
	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 1);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 1);

	led_set_color_power(EC_LED_COLOR_RED);
	led_set_color_power(EC_LED_COLOR_RED);
	led_set_color_battery(EC_LED_COLOR_RED);
	led_set_color_battery(EC_LED_COLOR_RED);

	led_set_color_battery(EC_LED_COLOR_GREEN);
	led_set_color_battery(EC_LED_COLOR_GREEN);

	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 1);
}

ZTEST(pirrha, test_led_brightness_range)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { 0 };

	const struct gpio_dt_spec *led_r =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_r);
	const struct gpio_dt_spec *led_g =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_g);
	const struct gpio_dt_spec *led_b =
		GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_b);

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 1);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 1);
	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 1);

	/* Verify LED colors defined in device tree are reflected in the
	 * brightness array.
	 */
	led_get_brightness_range(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_RED], 1);
	zassert_equal(brightness[EC_LED_COLOR_GREEN], 1);

	memset(brightness, 0, sizeof(brightness));

	led_get_brightness_range(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_BLUE], 1);

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_GREEN] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);

	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 1);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 0);
	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 1);

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_RED] = 1;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);

	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 0);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 1);
	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 1);

	memset(brightness, 0, sizeof(brightness));
	brightness[EC_LED_COLOR_BLUE] = 1;
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);

	zassert_equal(gpio_emul_output_get(led_r->port, led_r->pin), 1);
	zassert_equal(gpio_emul_output_get(led_g->port, led_g->pin), 1);
	zassert_equal(gpio_emul_output_get(led_b->port, led_b->pin), 0);
}

ZTEST(pirrha, test_board_vconn_control)
{
	const struct gpio_dt_spec *cc1 =
		GPIO_DT_FROM_NODELABEL(gpio_en_usb_c0_cc1_vconn);
	const struct gpio_dt_spec *cc2 =
		GPIO_DT_FROM_NODELABEL(gpio_en_usb_c0_cc2_vconn);

	/* Both off initially */
	gpio_pin_set_dt(cc1, 0);
	gpio_pin_set_dt(cc2, 0);

	/* Port 1 isn't managed through this function */
	board_pd_vconn_ctrl(1, USBPD_CC_PIN_1, 1);
	zassert_false(gpio_emul_output_get(cc1->port, cc1->pin));

	/* We can enable or disable CC1 */
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_1, 1);
	zassert_true(gpio_emul_output_get(cc1->port, cc1->pin));
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_1, 0);
	zassert_false(gpio_emul_output_get(cc1->port, cc1->pin));

	/* .. or CC2 */
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_2, 1);
	zassert_true(gpio_emul_output_get(cc2->port, cc2->pin));
	board_pd_vconn_ctrl(0, USBPD_CC_PIN_2, 0);
	zassert_false(gpio_emul_output_get(cc2->port, cc2->pin));
}
