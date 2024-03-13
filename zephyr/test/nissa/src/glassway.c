/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "driver/charger/isl923x_public.h"
#include "driver/tcpm/raa489000.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "fan.h"
#include "glassway.h"
#include "glassway_sub_board.h"
#include "hooks.h"
#include "led_onoff_states.h"
#include "led_pwm.h"
#include "mock/isl923x.h"
#include "pwm_mock.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "usb_charge.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpc_port0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpc_port1))
#define ASSERT_GPIO_FLAGS(spec, expected)                                  \
	do {                                                               \
		gpio_flags_t flags;                                        \
		zassert_ok(gpio_emul_flags_get((spec)->port, (spec)->pin,  \
					       &flags));                   \
		zassert_equal(flags, expected,                             \
			      "actual value was %#x; expected %#x", flags, \
			      expected);                                   \
	} while (0)

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

void fan_init(void);

FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(fan_set_count, int);
FAKE_VOID_FUNC(led_set_color_battery, enum ec_led_colors);
FAKE_VALUE_FUNC(int, raa489000_enable_asgate, int, bool);
FAKE_VALUE_FUNC(int, raa489000_set_output_current, int, enum tcpc_rp_value);
FAKE_VOID_FUNC(raa489000_hibernate, int, bool);
FAKE_VALUE_FUNC(enum ec_error_list, raa489000_is_acok, int, bool *);
FAKE_VOID_FUNC(extpower_handle_update, int);
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port);
FAKE_VALUE_FUNC(enum ec_error_list, charger_discharge_on_ac, int);
FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VOID_FUNC(usb_charger_task_set_event_sync, int, uint8_t);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);

void board_usb_pd_count_init(void);
static uint32_t fw_config_value;

static void set_fw_config_value(uint32_t value)
{
	fw_config_value = value;
	board_usb_pd_count_init();
	fan_init();
}

static void test_before(void *fixture)
{
	RESET_FAKE(raa489000_enable_asgate);
	RESET_FAKE(raa489000_set_output_current);
	RESET_FAKE(raa489000_hibernate);
	RESET_FAKE(raa489000_is_acok);
	RESET_FAKE(extpower_handle_update);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(fan_set_count);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	glassway_cached_sub_board = GLASSWAY_SB_1C_1A;
	set_fw_config_value(FW_SUB_BOARD_3);
}

ZTEST_SUITE(glassway, NULL, NULL, test_before, NULL, NULL);

ZTEST(glassway, test_charger_hibernate)
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

ZTEST(glassway, test_check_extpower)
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

ZTEST(glassway, test_is_sourcing_vbus)
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

ZTEST(glassway, test_set_active_charge_port_none)
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

ZTEST(glassway, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(4), EC_ERROR_INVAL,
		      "port 4 doesn't exist, should return error");
}

ZTEST(glassway, test_set_active_charge_port_currently_sourcing)
{
	/* Attempting to sink on a port that's sourcing is an error */
	tcpci_emul_set_reg(TCPC1, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
}

ZTEST(glassway, test_set_active_charge_port)
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

ZTEST(glassway, test_set_active_charge_port_enable_fail)
{
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);

	/* Charging was enabled again after the error */
	zassert_equal(charger_discharge_on_ac_fake.arg0_val, 0);
}

ZTEST(glassway, test_set_active_charge_port_disable_fail)
{
	/* Failing to disable sinking on the other port isn't fatal */
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		TCPC_REG_COMMAND);
	zassert_ok(board_set_active_charge_port(0));
}

ZTEST(glassway, test_tcpc_get_alert_status)
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

ZTEST(glassway, test_pd_power_supply_reset)
{
	uint16_t reg;

	/* Stops any active sourcing on the given port */
	pd_power_supply_reset(0);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SRC_CTRL_LOW);
}

ZTEST(glassway, test_set_source_current_limit)
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

ZTEST(glassway, test_pd_set_power_supply_ready)
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

ZTEST(glassway, test_reset_pd_mcu)
{
	/* Doesn't do anything */
	board_reset_pd_mcu();
}

ZTEST(glassway, test_process_pd_alert)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);

	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	board_process_pd_alert(0);
	/*
	 * This should also call schedule_deferred_pd_interrupt() again, but
	 * there's no good way to verify that.
	 */

	/* Port 1 also works */
	gpio_emul_input_set(c1_int->port, c1_int->pin, 0);
	board_process_pd_alert(1);
}

static int get_fan_config_present(enum cbi_fw_config_field_id field,
				  uint32_t *value)
{
	zassert_equal(field, FW_THERMAL_SOLUTION);
	*value = FW_THERMAL_SOLUTION_ACTIVE;
	return 0;
}

static int get_fan_config_absent(enum cbi_fw_config_field_id field,
				 uint32_t *value)
{
	zassert_equal(field, FW_THERMAL_SOLUTION);
	*value = FW_THERMAL_SOLUTION_PASSIVE;
	return 0;
}

ZTEST(glassway, test_fan_present)
{
	int flags;

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_present;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW,
		      "actual GPIO flags were %#x", flags);
}

ZTEST(glassway, test_fan_absent)
{
	int flags;
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
			      GPIO_DISCONNECTED);

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_absent;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 1,
		      "function actually called %d times",
		      fan_set_count_fake.call_count);
	zassert_equal(fan_set_count_fake.arg0_val, 0, "parameter value was %d",
		      fan_set_count_fake.arg0_val);

	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}

ZTEST(glassway, test_fan_cbi_error)
{
	int flags;
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
			      GPIO_DISCONNECTED);

	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}

enum glassway_sub_board_type glassway_get_sb_type(void);

static int
get_fake_sub_board_fw_config_field(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

int init_gpios(const struct device *unused);

ZTEST(glassway, test_db_without_c)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	/* Reset cached global state. */
	glassway_cached_sub_board = GLASSWAY_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_2);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1A);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);

	glassway_cached_sub_board = GLASSWAY_SB_1C_1A;
	fw_config_value = -1;
}

ZTEST(glassway, test_db_with_c)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	/* Reset cached global state. */
	glassway_cached_sub_board = GLASSWAY_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_1);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);

	glassway_cached_sub_board = GLASSWAY_SB_1C;
	fw_config_value = -1;

	/* Reset cached global state. */
	glassway_cached_sub_board = GLASSWAY_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_3);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C_1A);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);

	glassway_cached_sub_board = GLASSWAY_SB_1C_1A;
	fw_config_value = -1;
}

ZTEST(glassway, test_led)
{
	led_set_color_battery(EC_LED_COLOR_WHITE);
	zassert_equal(led_set_color_battery_fake.arg0_val, EC_LED_COLOR_WHITE);

	led_set_color_battery(EC_LED_COLOR_AMBER);
	zassert_equal(led_set_color_battery_fake.arg0_val, EC_LED_COLOR_AMBER);
}
