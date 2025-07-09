/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/isl923x_public.h"
#include "driver/tcpm/raa489000.h"
#include "emul/retimer/emul_anx7483.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "fan.h"
#include "glassway.h"
#include "glassway_sub_board.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "led_onoff_states.h"
#include "led_pwm.h"
#include "mock/isl923x.h"
#include "motionsense_sensors.h"
#include "nissa_hdmi.h"
#include "pwm_mock.h"
#include "system.h"
#include "tablet_mode.h"
#include "tcpm/tcpci.h"
#include "usb_charge.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <drivers/vivaldi_kbd.h>
#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpc_port0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpc_port1))
#define ANX7483_EMUL1 EMUL_DT_GET(DT_NODELABEL(anx7483_port1))
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
void board_power_change(struct ap_power_ev_callback *cb,
			struct ap_power_ev_data data);

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
FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VALUE_FUNC(int, board_get_battery_soc);
FAKE_VALUE_FUNC(int, cbi_get_ssfc, uint32_t *);
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(icm42607_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(nissa_configure_hdmi_rails);
FAKE_VOID_FUNC(lpc_keyboard_resume_irq);

void board_usb_pd_count_init(void);
static uint32_t fw_config_value;

enum glassway_sub_board_type glassway_get_sb_type(void);

static int
get_fake_sub_board_fw_config_field(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

static int get_gpio_output(const struct gpio_dt_spec *const spec)
{
	return gpio_emul_output_get(spec->port, spec->pin);
}

struct glassway_sub_board_fixture {
	const struct gpio_dt_spec *sb_1;
	const struct gpio_dt_spec *sb_2;
	const struct gpio_dt_spec *sb_3;
	const struct gpio_dt_spec *sb_4;
};

static void *suite_setup_fn()
{
	struct glassway_sub_board_fixture *fixture =
		k_malloc(sizeof(struct glassway_sub_board_fixture));

	zassume_not_null(fixture);
	fixture->sb_1 = GPIO_DT_FROM_NODELABEL(gpio_sb_1);
	fixture->sb_2 = GPIO_DT_FROM_NODELABEL(gpio_sb_2);
	fixture->sb_3 = GPIO_DT_FROM_NODELABEL(gpio_sb_3);
	fixture->sb_4 = GPIO_DT_FROM_NODELABEL(gpio_sb_4);

	return fixture;
}

static void test_before_fn(void *fixture_)
{
	struct glassway_sub_board_fixture *fixture = fixture_;

	/* Reset cached global state. */
	glassway_cached_sub_board = GLASSWAY_SB_UNKNOWN;
	fw_config_value = -1;

	/* Return the fake fw_config value. */
	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;

	/* Unconfigure sub-board GPIOs. */
	gpio_pin_configure_dt(fixture->sb_1, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(fixture->sb_2, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(fixture->sb_3, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(fixture->sb_4, GPIO_DISCONNECTED);
	/* Reset C1 interrupt to deasserted. */
	gpio_emul_input_set(fixture->sb_1->port, fixture->sb_1->pin, 1);

	RESET_FAKE(usb_interrupt_c1);
}
ZTEST_SUITE(glassway_sub_board, NULL, suite_setup_fn, test_before_fn, NULL,
	    NULL);

static void set_sb_config(uint32_t value)
{
	glassway_cached_sub_board = GLASSWAY_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	fw_config_value = value;
	board_usb_pd_count_init();
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
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(icm42607_interrupt);
	RESET_FAKE(cbi_get_ssfc);
	RESET_FAKE(nissa_configure_hdmi_rails);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	/*
	 * Clear cached sub-board ID from CBI and reset to the default
	 * sub-board (side effect: cros_cbi_get_fw_config_fake is configured).
	 */
	set_sb_config(GLASSWAY_SB_1C_1A);
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

	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	fan_init();

	zassert_equal(fan_set_count_fake.call_count, 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}

int init_gpios(const struct device *unused);

ZTEST(glassway, test_db_without_c)
{
	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_2);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1A);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_5);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_HDMI_LTE);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);
}

ZTEST(glassway, test_db_with_c)
{
	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_1);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_3);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C_1A);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_4);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C_LTE);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_sb_1),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);
}

ZTEST(glassway, test_db_with_hdmi)
{
	nissa_configure_hdmi_rails_fake.call_count = 0;
	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_1);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 0);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_2);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1A);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 0);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_3);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C_1A);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 0);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_4);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C_LTE);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 0);

	/* Set the sub-board, reported configuration is correct. */
	set_sb_config(FW_SUB_BOARD_5);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_HDMI_LTE);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 1);
}

ZTEST_F(glassway_sub_board, test_db_with_lte)
{
	set_sb_config(FW_SUB_BOARD_4);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_1C_LTE);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	/* GPIOs are configured as expected. */
	ASSERT_GPIO_FLAGS(fixture->sb_2 /* Standby power enable */,
			  GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);

	/* LTE power gets enabled on S5. */
	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_equal(get_gpio_output(fixture->sb_2), 1);
	/* And disabled on G3. */
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(get_gpio_output(fixture->sb_2), 0);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	set_sb_config(FW_SUB_BOARD_5);
	zassert_equal(glassway_get_sb_type(), GLASSWAY_SB_HDMI_LTE);

	/* GPIOs are configured as expected. */
	ASSERT_GPIO_FLAGS(fixture->sb_2 /* Standby power enable */,
			  GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);

	/* LTE power gets enabled on S5. */
	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_equal(get_gpio_output(fixture->sb_2), 1);
	/* And disabled on G3. */
	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(get_gpio_output(fixture->sb_2), 0);
	hook_notify(HOOK_INIT);
}

ZTEST(glassway, test_led)
{
	led_set_color_battery(EC_LED_COLOR_WHITE);
	zassert_equal(led_set_color_battery_fake.arg0_val, EC_LED_COLOR_WHITE);

	led_set_color_battery(EC_LED_COLOR_AMBER);
	zassert_equal(led_set_color_battery_fake.arg0_val, EC_LED_COLOR_AMBER);
}

static bool clamshell_mode;

static int cbi_get_form_factor_config(enum cbi_fw_config_field_id field,
				      uint32_t *value)
{
	if (field == FORM_FACTOR)
		*value = clamshell_mode ? CLAMSHELL : CONVERTIBLE;

	return 0;
}

ZTEST(glassway, test_convertible)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	int interrupt_count;

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	clamshell_mode = false;
	board_setup_init();

	/* Verify gmr_tablet_switch is enabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;

	/* Verify base_imu_irq is enabled. Interrupt is configured
	 * GPIO_INT_EDGE_FALLING, so set high, then set low.
	 */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 1);
}

ZTEST(glassway, test_clamshell)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	int interrupt_count;

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	clamshell_mode = true;
	board_setup_init();

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;

	/* Verify base_imu_irq is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 0);
}

static int ssfc_data;

static int cbi_get_ssfc_mock(uint32_t *ssfc)
{
	*ssfc = ssfc_data;

	return 0;
}

ZTEST(glassway, test_board_setup_init_convertible)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);
	int interrupt_count;

	/* Initial ssfc data for BMA422 and BMI323. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x00;
	cros_cbi_ssfc_init();

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;
	clamshell_mode = false;
	board_setup_init();

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);

	/* Clear base and lid sensor interrupt call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;

	/* Verify base and lid sensor interrupt is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count +
			  bma4xx_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 2);
	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(icm42607_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
}

ZTEST(glassway, test_alt_sensor)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	/* Initial ssfc data for LSM6DSM and LIS2DW. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x8;
	cros_cbi_ssfc_init();

	/* Enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	alt_sensor_init();

	/* Clear base interrupt call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	icm42607_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(icm42607_interrupt_fake.call_count, 1);
}

static int gpio_emul_output_get_dt(const struct gpio_dt_spec *dt)
{
	return gpio_emul_output_get(dt->port, dt->pin);
}

static int gpio_emul_input_set_dt(const struct gpio_dt_spec *dt, int value)
{
	return gpio_emul_input_set(dt->port, dt->pin, value);
}

ZTEST(glassway, test_pen_detect_interrupt)
{
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);
	const struct gpio_dt_spec *const pen_irq =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);

	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pen_det_l));
	gpio_emul_input_set(pen_irq->port, pen_irq->pin, 0);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 1);

	/*  De-assert the IRQ */
	gpio_emul_input_set(pen_irq->port, pen_irq->pin, 1);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 0);
}

ZTEST(glassway, test_pen_power_control)
{
	const struct gpio_dt_spec *const pen_detect_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_pen_detect_odl);
	const struct gpio_dt_spec *const pen_power_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);
	const struct gpio_int_config *const pen_detect_int =
		GPIO_INT_FROM_NODELABEL(int_pen_det_l);
	struct ap_power_ev_data data;

	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set_dt(pen_detect_gpio, 1), NULL);

	data.event = AP_POWER_STARTUP;
	board_power_change(NULL, data);
	/* gpio_en_pp5000_pen_x become high */
	gpio_enable_dt_interrupt(pen_detect_int);
	zassert_ok(gpio_emul_input_set_dt(pen_detect_gpio, 0), NULL);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 1);

	/* gpio_en_pp5000_pen_x keep low if AP_POWER_SHUTDOWN */
	data.event = AP_POWER_SHUTDOWN;
	board_power_change(NULL, data);
	gpio_disable_dt_interrupt(pen_detect_int);
	zassert_equal(gpio_emul_output_get_dt(pen_power_gpio), 0);
}

ZTEST(glassway, test_board_anx7483_c1_mux_set)
{
	int rv;
	enum anx7483_eq_setting eq;

	/* Initial ssfc data for Glassway FFC connector. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x00;
	cros_cbi_ssfc_init();

	usb_mux_init(1);

	/* Test USB mux state. */
	usb_mux_set(1, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	/* Test dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	/* Test flipped dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED,
		    USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_3DB);

	/* Initial ssfc data for Gallida360 FFC connector. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x20;
	cros_cbi_ssfc_init();
	cbi_set_ssfc(0x20);

	usb_mux_init(1);

	/* Test USB mux state. */
	usb_mux_set(1, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_6_8DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_6_8DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_8DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_8DB);

	/* Test dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_8DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_6_8DB);

	/* Test flipped dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED,
		    USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_URX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_7_8DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_6_8DB);
}

int clock_get_freq(void)
{
	return 16000000;
}

static bool keyboard_ca_fr;

static int cbi_get_keyboard_type_config(enum cbi_fw_config_field_id field,
					uint32_t *value)
{
	if (field != FW_KB_TYPE)
		return -EINVAL;

	zassert_equal(field, FW_KB_TYPE);
	*value = keyboard_ca_fr ? FW_KB_TYPE_CA_FR : FW_KB_TYPE_DEFAULT;
	return 0;
}

ZTEST(glassway, test_keyboard_type)
{
	uint16_t forwardslash_pipe_key = get_scancode_set2(2, 7);
	uint16_t right_control_key = get_scancode_set2(4, 0);

	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), right_control_key);
	zassert_equal(get_scancode_set2(2, 7), forwardslash_pipe_key);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_keyboard_type_config;

	keyboard_ca_fr = false;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), right_control_key);
	zassert_equal(get_scancode_set2(2, 7), forwardslash_pipe_key);

	keyboard_ca_fr = true;
	kb_init();
	zassert_equal(get_scancode_set2(4, 0), forwardslash_pipe_key);
	zassert_equal(get_scancode_set2(2, 7), right_control_key);
}
