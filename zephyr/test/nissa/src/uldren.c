/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "driver/accelgyro_lsm6dso.h"
#include "driver/mp2964.h"
#include "emul/retimer/emul_anx7483.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_backlight.h"
#include "lid_switch.h"
#include "mock/isl923x.h"
#include "motionsense_sensors.h"
#include "system.h"
#include "tablet_mode.h"
#include "tcpm/tcpci.h"
#include "uldren.h"
#include "uldren_sub_board.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include <dt-bindings/buttons.h>
#include <dt-bindings/gpio_defines.h>
#include <typec_control.h>

#define TCPC0 EMUL_DT_GET(DT_NODELABEL(tcpc_port0))
#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpc_port1))

#define ANX7483_EMUL1 EMUL_DT_GET(DT_NODELABEL(anx7483_port1))

#define TEST_LID_DEBOUNCE_MS (LID_DEBOUNCE_US / MSEC + 1)

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

FAKE_VALUE_FUNC(int, chipset_in_state, int);

FAKE_VALUE_FUNC(enum ec_error_list, raa489000_is_acok, int, bool *);
FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VOID_FUNC(raa489000_hibernate, int, bool);

FAKE_VALUE_FUNC(int, raa489000_enable_asgate, int, bool);
FAKE_VALUE_FUNC(int, raa489000_set_output_current, int, enum tcpc_rp_value);
FAKE_VALUE_FUNC(enum ec_error_list, charger_discharge_on_ac, int);
FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(int, mp2964_tune, const struct mp2964_reg_val *, int,
		const struct mp2964_reg_val *, int);

FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lsm6dso_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);

int button_disable_gpio(enum button button_type)
{
	return EC_SUCCESS;
}

static int get_gpio_output(const struct gpio_dt_spec *const spec)
{
	return gpio_emul_output_get(spec->port, spec->pin);
}

void board_usb_pd_count_init(void);
static uint32_t fw_config_value;

/** Set the value of the CBI fw_config field returned by the fake. */
static void set_fw_config_value(uint32_t value)
{
	fw_config_value = value;
	board_usb_pd_count_init();
}

static void test_before(void *fixture)
{
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dso_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);
	RESET_FAKE(raa489000_hibernate);
	RESET_FAKE(raa489000_is_acok);
	RESET_FAKE(raa489000_enable_asgate);
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(cros_cbi_get_fw_config);

	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;

	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC1),
		I2C_COMMON_EMUL_NO_FAIL_REG);

	uldren_cached_sub_board = ULDREN_SB_C;
	set_fw_config_value(FW_SUB_BOARD_2);
}
ZTEST_SUITE(uldren, NULL, NULL, test_before, NULL, NULL);

ZTEST(uldren, test_extpower_is_present)
{
	/* Errors are not-OK */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_error;
	zassert_false(extpower_is_present());
	zassert_equal(raa489000_is_acok_fake.call_count, 2);

	/* When neither charger is connected, we check both and return no. */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;
	zassert_false(extpower_is_present());
	zassert_equal(raa489000_is_acok_fake.call_count, 4);

	/* If one is connected, AC is present */
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_present;
	zassert_true(extpower_is_present());
	zassert_equal(raa489000_is_acok_fake.call_count, 5);
}

static int extpower_handle_update_call_count;

static void call_extpower_handle_update(void)
{
	extpower_handle_update_call_count++;
}
DECLARE_HOOK(HOOK_AC_CHANGE, call_extpower_handle_update, HOOK_PRIO_DEFAULT);

ZTEST(uldren, test_board_check_extpower)
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

ZTEST(uldren, test_charger_hibernate)
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

ZTEST(uldren, test_board_is_sourcing_vbus)
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

ZTEST(uldren, test_set_active_charge_port_none)
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

ZTEST(uldren, test_set_active_charge_port_invalid_port)
{
	zassert_equal(board_set_active_charge_port(4), EC_ERROR_INVAL,
		      "port 4 doesn't exist, should return error");
}

ZTEST(uldren, test_set_active_charge_port_currently_sourcing)
{
	/* Attempting to sink on a port that's sourcing is an error */
	tcpci_emul_set_reg(TCPC1, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
}

ZTEST(uldren, test_set_active_charge_port)
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

ZTEST(uldren, test_set_active_charge_port_enable_fail)
{
	i2c_common_emul_set_write_fail_reg(
		emul_tcpci_generic_get_i2c_common_data(TCPC0),
		TCPC_REG_COMMAND);
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
}

ZTEST(uldren, test_tcpc_get_alert_status)
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

ZTEST(uldren, test_pd_power_supply_reset)
{
	uint16_t reg;

	/* Stops any active sourcing on the given port */
	pd_power_supply_reset(0);
	tcpci_emul_get_reg(TCPC0, TCPC_REG_COMMAND, &reg);
	zassert_equal(reg, TCPC_REG_COMMAND_SRC_CTRL_LOW);
}

ZTEST(uldren, test_set_source_current_limit)
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

ZTEST(uldren, test_pd_set_power_supply_ready)
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

ZTEST(uldren, test_reset_pd_mcu)
{
	/* Doesn't do anything */
	board_reset_pd_mcu();
}

ZTEST(uldren, test_process_pd_alert)
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

static bool kb_backlight_sku;

static int cbi_get_kb_bl_fw_config(enum cbi_fw_config_field_id field,
				   uint32_t *value)
{
	zassert_equal(field, FW_KB_BL);
	*value = kb_backlight_sku ? FW_KB_BL_PRESENT : FW_KB_BL_NOT_PRESENT;
	return 0;
}

ZTEST(uldren, test_keyboard_backlight)
{
	/* For PLATFORM_EC_PWM_KBLIGHT default enabled, EC_FEATURE_PWM_KEYB
	 * is set.
	 */
	uint32_t flags0 = EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB);
	uint32_t result;

	/* Support keyboard backlight */
	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_kb_bl_fw_config;
	kb_backlight_sku = true;
	result = board_override_feature_flags0(flags0);
	zassert_equal(result, flags0,
		      "Support kblight, should keep PWM_KEYB feature.");

	/* Error reading fw_config */
	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	result = board_override_feature_flags0(flags0);
	zassert_equal(result, flags0,
		      "Unchange ec feature, keep PWM_KEYB feature.");

	/* Not support keyboard backlight */
	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_kb_bl_fw_config;
	kb_backlight_sku = false;
	result = board_override_feature_flags0(flags0);
	zassert_equal(result, 0, "No kblight should clear PWM_KEYB feature.");
}

enum uldren_sub_board_type uldren_get_sb_type(void);

static int
get_fake_sub_board_fw_config_field(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

static int cbi_get_board_version_1(uint32_t *version)
{
	*version = 1;
	return 0;
}

static int cbi_get_board_version_2(uint32_t *version)
{
	*version = 2;
	return 0;
}

static int cbi_get_board_version_3(uint32_t *version)
{
	*version = 3;
	return 0;
}

/* Shim GPIO initialization from devicetree. */
int init_gpios(const struct device *unused);

ZTEST(uldren, test_usb_c)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_subboard_detect_l);

	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	uldren_cached_sub_board = ULDREN_SB_UNKNOWN;
	fw_config_value = -1;
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;

	/* Set the sub-board, reported configuration is correct. */
	gpio_emul_input_set(c0_int->port, c0_int->pin, 1);
	set_fw_config_value(FW_SUB_BOARD_2);
	zassert_equal(uldren_get_sb_type(), ULDREN_SB_C);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	/* Should have fetched CBI exactly once, asking for the sub-board. */
	zassert_equal(cros_cbi_get_fw_config_fake.call_count, 1);
	zassert_equal(cros_cbi_get_fw_config_fake.arg0_history[0],
		      FW_SUB_BOARD);

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;
	set_fw_config_value(FW_SUB_BOARD_2);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	set_fw_config_value(FW_SUB_BOARD_2);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_subboard_detect_l),
			  GPIO_PULL_UP | GPIO_INPUT);

	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_sb_2)), 1);

	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_sb_2)), 0);

	uldren_cached_sub_board = ULDREN_SB_C;
	fw_config_value = -1;
}

ZTEST(uldren, test_usb_c_lte)
{
	const struct gpio_dt_spec *c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_subboard_detect_l);

	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	uldren_cached_sub_board = ULDREN_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	gpio_emul_input_set(c0_int->port, c0_int->pin, 1);
	set_fw_config_value(FW_SUB_BOARD_3);
	zassert_equal(uldren_get_sb_type(), ULDREN_SB_C_LTE);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	/* Should have fetched CBI exactly once, asking for the sub-board. */
	zassert_equal(cros_cbi_get_fw_config_fake.call_count, 1);
	zassert_equal(cros_cbi_get_fw_config_fake.arg0_history[0],
		      FW_SUB_BOARD);

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;
	set_fw_config_value(FW_SUB_BOARD_3);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	gpio_emul_input_set(c0_int->port, c0_int->pin, 0);
	set_fw_config_value(FW_SUB_BOARD_3);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_subboard_detect_l),
			  GPIO_PULL_UP | GPIO_INPUT);

	ap_power_ev_send_callbacks(AP_POWER_PRE_INIT);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_sb_2)), 1);

	ap_power_ev_send_callbacks(AP_POWER_HARD_OFF);
	zassert_equal(get_gpio_output(GPIO_DT_FROM_NODELABEL(gpio_sb_2)), 0);

	uldren_cached_sub_board = ULDREN_SB_C_LTE;
	fw_config_value = -1;
}

ZTEST(uldren, test_unset_board)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	uldren_cached_sub_board = ULDREN_SB_UNKNOWN;
	fw_config_value = -1;
	/* fw_config with an unset sub-board means none is present. */
	set_fw_config_value(ULDREN_SB_NONE);
	zassert_equal(uldren_get_sb_type(), ULDREN_SB_NONE);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);
}

static int get_fw_config_error(enum cbi_fw_config_field_id field,
			       uint32_t *value)
{
	return EC_ERROR_UNKNOWN;
}

ZTEST(uldren, test_cbi_error)
{
	/*
	 * Reading fw_config from CBI returns an error, so sub-board is treated
	 * as unknown.
	 */
	uldren_cached_sub_board = ULDREN_SB_UNKNOWN;
	fw_config_value = -1;

	cros_cbi_get_fw_config_fake.custom_fake = get_fw_config_error;
	zassert_equal(uldren_get_sb_type(), ULDREN_SB_NONE);
}
ZTEST(uldren, test_board_anx7483_c1_mux_set)
{
	int rv;
	enum anx7483_eq_setting eq;

	usb_mux_init(1);

	/* Test USB mux state. */
	usb_mux_set(1, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	/* Test dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX1, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	/* Test flipped dock mux state. */
	usb_mux_set(1, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED,
		    USB_SWITCH_CONNECT, 0);

	rv = anx7483_emul_get_eq(ANX7483_EMUL1, ANX7483_PIN_DRX2, &eq);
	zassert_ok(rv);
	zassert_equal(eq, ANX7483_EQ_SETTING_12_5DB);
}

ZTEST(uldren, test_mp2964_on_startup)
{
	const struct gpio_dt_spec *lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_lid_open);

	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 0), NULL);
	k_sleep(K_MSEC(TEST_LID_DEBOUNCE_MS));

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_3;

	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(mp2964_tune_fake.call_count, 1);
	hook_notify(HOOK_CHIPSET_STARTUP);
	zassert_equal(mp2964_tune_fake.call_count, 1);
}

static int get_fake_sensor_fw_config_field(enum cbi_fw_config_field_id field_id,
					   uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

static bool tablet_present;

static int cbi_get_tablet_fw_config(enum cbi_fw_config_field_id field,
				    uint32_t *value)
{
	if (field != FW_TABLET)
		return -EINVAL;

	*value = tablet_present ? FW_TABLET_PRESENT : FW_TABLET_NOT_PRESENT;
	return 0;
}

ZTEST(uldren, test_bma422_lsm6dso)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sensor_fw_config_field;

	set_fw_config_value(BMA422_LSM6DSO);

	/* sensor_enable_irqs enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;

	hook_notify(HOOK_INIT);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 1);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
}

ZTEST(uldren, test_bma422_bmi323)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sensor_fw_config_field;
	set_fw_config_value(BMA422_BMI323);

	/* sensor_enable_irqs enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;

	hook_notify(HOOK_INIT);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_tablet_fw_config;

	tablet_present = true;

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;

	hook_notify(HOOK_INIT);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
}

ZTEST(uldren, test_lis2dw12_bmi323)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sensor_fw_config_field;
	set_fw_config_value(LIS2DW12_BMI323);

	/* sensor_enable_irqs enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;

	hook_notify(HOOK_INIT);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
}

ZTEST(uldren, test_lis2dw12_lsm6dso)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sensor_fw_config_field;

	set_fw_config_value(LIS2DW12_LSM6DSO);

	/* sensor_enable_irqs enable the interrupt int_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;

	hook_notify(HOOK_INIT);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 1);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_tablet_fw_config;

	tablet_present = true;

	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;

	hook_notify(HOOK_INIT);

	/* Clear base_imu_irq call count before test */
	bmi3xx_interrupt_fake.call_count = 0;
	lsm6dso_interrupt_fake.call_count = 0;
	bma4xx_interrupt_fake.call_count = 0;
	lis2dw12_interrupt_fake.call_count = 0;

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dso_interrupt_fake.call_count, 1);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
}

static int chipset_state;

static int chipset_in_state_mock(int state_mask)
{
	if (state_mask & chipset_state)
		return 1;

	return 0;
}

ZTEST(uldren, test_touchpad_enable_switch)
{
	const struct gpio_dt_spec *lid_open =
		GPIO_DT_FROM_NODELABEL(gpio_lid_open);
	const struct gpio_dt_spec *touch_lid_en =
		GPIO_DT_FROM_NODELABEL(gpio_tchpad_lid_close);

	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	chipset_state = CHIPSET_STATE_ANY_SUSPEND;

	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 1), NULL);
	k_sleep(K_MSEC(TEST_LID_DEBOUNCE_MS));

	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_equal(
		gpio_emul_output_get(touch_lid_en->port, touch_lid_en->pin), 1);

	zassert_ok(gpio_emul_input_set(lid_open->port, lid_open->pin, 0), NULL);
	k_sleep(K_MSEC(TEST_LID_DEBOUNCE_MS));

	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_equal(
		gpio_emul_output_get(touch_lid_en->port, touch_lid_en->pin), 0);
}
