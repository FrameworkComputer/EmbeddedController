/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "board_config.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "craaskov.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "mock/isl923x.h"
#include "nissa_hdmi.h"
#include "system.h"
#include "tcpm/tcpci.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpc_port0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpc_port1))

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VOID_FUNC(nissa_configure_hdmi_rails);
FAKE_VOID_FUNC(nissa_configure_hdmi_vcc);
FAKE_VALUE_FUNC(int, raa489000_set_output_current, int, enum tcpc_rp_value);
FAKE_VALUE_FUNC(enum ec_error_list, raa489000_is_acok, int, bool *);
FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VALUE_FUNC(enum ec_error_list, charger_discharge_on_ac, int);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(set_scancode_set2, uint8_t, uint8_t, uint16_t);
FAKE_VOID_FUNC(get_scancode_set2, uint8_t, uint8_t);

FAKE_VOID_FUNC(raa489000_hibernate, int, bool);
FAKE_VALUE_FUNC(int, raa489000_enable_asgate, int, bool);

static void test_before(void *fixture)
{
	RESET_FAKE(raa489000_is_acok);
	RESET_FAKE(raa489000_hibernate);
	RESET_FAKE(raa489000_enable_asgate);
	RESET_FAKE(raa489000_set_output_current);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(set_scancode_set2);
	RESET_FAKE(get_scancode_set2);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
}
ZTEST_SUITE(craaskov, NULL, NULL, test_before, NULL, NULL);

static int cbi_get_board_version_0(uint32_t *version)
{
	*version = 0;
	return 0;
}

static int cbi_get_board_version_1(uint32_t *version)
{
	*version = 1;
	return 1;
}

ZTEST(craaskov, test_hdmi_power)
{
	/* BOARD version < 1, set hdmi vcc */
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_0;
	nissa_configure_hdmi_power_gpios();
	zassert_equal(nissa_configure_hdmi_vcc_fake.call_count, 1);

	/* Board version = 1, do not set hdmi vcc  */
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;
	nissa_configure_hdmi_power_gpios();
	zassert_equal(nissa_configure_hdmi_vcc_fake.call_count, 1);
}

ZTEST(craaskov, test_board_is_sourcing_vbus)
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

ZTEST(craaskov, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(4), EC_ERROR_INVAL,
		      "port 4 doesn't exist, should return error");
}

ZTEST(craaskov, test_set_active_charge_port_currently_sourcing)
{
	/* Attempting to sink on a port that's sourcing is an error */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_INVAL);
}

ZTEST(craaskov, test_set_active_charge_port_none)
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
}

ZTEST(craaskov, test_set_active_charge_port)
{
	uint16_t reg;

	/* Setting old_port to a port not CHARGE_PORT_NONE. */
	charge_port = 1;
	/* We can successfully start sinking on a port */
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(raa489000_enable_asgate_fake.call_count, 2);
	zassert_equal(charger_discharge_on_ac_fake.call_count, 2);

	/* Requested charging stop initially */
	zassert_equal(charger_discharge_on_ac_fake.arg0_history[0], 1);

	/* Sinking was enabled on the new port */
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SNK_CTRL_HIGH);
	zassert_equal(raa489000_enable_asgate_fake.arg0_history[1], 0);
	zassert_equal(raa489000_enable_asgate_fake.arg1_history[1], true);
	/* Resumed charging */
	zassert_equal(charger_discharge_on_ac_fake.arg0_history[1], 0);
}

ZTEST(craaskov, test_tcpc_get_alert_status)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);

	/* Both IRQs are asserted */
	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);

	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 1);
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_0);

	/* Bit 14 is ignored */
	tcpci_emul_set_reg(TCPC0, TCPC_REG_ALERT, 0x4000);
	zassert_equal(tcpc_get_alert_status(), 0);
}

ZTEST(craaskov, test_pd_power_supply_reset)
{
	uint16_t reg;

	/* Stops any active sourcing on the given port */
	pd_power_supply_reset(0);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SRC_CTRL_LOW);
}

ZTEST(craaskov, test_set_source_current_limit)
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

ZTEST(craaskov, test_pd_set_power_supply_ready)
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

ZTEST(craaskov, test_reset_pd_mcu)
{
	/* Doesn't do anything */
	board_reset_pd_mcu();
}

static int extpower_handle_update_call_count;

static void call_extpower_handle_update(void)
{
	extpower_handle_update_call_count++;
}
DECLARE_HOOK(HOOK_AC_CHANGE, call_extpower_handle_update, HOOK_PRIO_DEFAULT);

ZTEST(craaskov, test_board_check_extpower)
{
	/* Clear call count before testing. */
	extpower_handle_update_call_count = 0;

	/* Update with no change does nothing. */
	board_check_extpower();
	zassert_equal(extpower_handle_update_call_count, 0);

	/* Becoming present updates */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_present;
	board_check_extpower();
	zassert_equal(extpower_handle_update_call_count, 1);

	/* Errors are treated as not plugged in */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_error;
	board_check_extpower();
	zassert_equal(extpower_handle_update_call_count, 2);
}

ZTEST(craaskov, test_charger_hibernate)
{
	/* board_hibernate() asks the chargers to hibernate. */
	board_hibernate();

	zassert_equal(raa489000_hibernate_fake.call_count, 1);

	zassert_true(raa489000_hibernate_fake.arg1_history[0]);
}

static int keyboard_layout;

static int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FW_KB_TYPE)
		return -EINVAL;

	switch (keyboard_layout) {
	case 0:
		*value = FW_KB_TYPE_DEFAULT;
		break;
	case 1:
		*value = FW_KB_TYPE_ANSI_CANADIAN;
		break;
	default:
		return 0;
	}
	return 0;
}

ZTEST(craaskov, test_kb_layout_init_cbi_error)
{
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 0);
	zassert_equal(get_scancode_set2_fake.call_count, 0);
}

ZTEST(craaskov, test_kb_layout_init)
{
	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;

	keyboard_layout = 0;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 0);
	zassert_equal(get_scancode_set2_fake.call_count, 0);

	keyboard_layout = 1;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 2);
}
