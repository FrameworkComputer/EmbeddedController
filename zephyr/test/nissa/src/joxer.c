/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "charge_manager.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "fan.h"
#include "hooks.h"
#include "joxer.h"
#include "joxer_sub_board.h"
#include "keyboard_protocol.h"
#include "motionsense_sensors.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "typec_control.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

#define TCPC1 EMUL_DT_GET(DT_NODELABEL(tcpci_emul_1))
#define LID_ACCEL SENSOR_ID(DT_NODELABEL(lid_accel))

#define ASSERT_GPIO_FLAGS(spec, expected)                                  \
	do {                                                               \
		gpio_flags_t flags;                                        \
		zassert_ok(gpio_emul_flags_get((spec)->port, (spec)->pin,  \
					       &flags));                   \
		zassert_equal(flags, expected,                             \
			      "actual value was %#x; expected %#x", flags, \
			      expected);                                   \
	} while (0)

FAKE_VALUE_FUNC(enum ec_error_list, sm5803_is_acok, int, bool *);
FAKE_VALUE_FUNC(bool, sm5803_check_vbus_level, int, enum vbus_level);
FAKE_VOID_FUNC(sm5803_disable_low_power_mode, int);
FAKE_VOID_FUNC(sm5803_enable_low_power_mode, int);
FAKE_VALUE_FUNC(enum ec_error_list, sm5803_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, sm5803_set_vbus_disch, int, int);
FAKE_VOID_FUNC(sm5803_hibernate, int);
FAKE_VOID_FUNC(sm5803_interrupt, int);
FAKE_VOID_FUNC(sm5803_handle_interrupt, int);
FAKE_VALUE_FUNC(enum ec_error_list, sm5803_get_chg_det, int, int *);

FAKE_VALUE_FUNC(enum ec_error_list, charger_set_otg_current_voltage, int, int,
		int);
FAKE_VALUE_FUNC(enum ec_error_list, charge_set_input_current_limit, int, int);
FAKE_VALUE_FUNC(enum ec_error_list, charger_enable_otg_power, int, int);
FAKE_VALUE_FUNC(int, charger_is_sourcing_otg_power, int);
FAKE_VOID_FUNC(extpower_handle_update, int);
FAKE_VOID_FUNC(charger_discharge_on_ac, int);
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port);
FAKE_VOID_FUNC(schedule_deferred_pd_interrupt, int);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);
FAKE_VOID_FUNC(set_scancode_set2, uint8_t, uint8_t, uint16_t);
FAKE_VOID_FUNC(get_scancode_set2, uint8_t, uint8_t);

uint8_t board_get_charger_chip_count(void)
{
	return 2;
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
	RESET_FAKE(sm5803_is_acok);
	RESET_FAKE(sm5803_check_vbus_level);
	RESET_FAKE(sm5803_disable_low_power_mode);
	RESET_FAKE(sm5803_enable_low_power_mode);
	RESET_FAKE(sm5803_vbus_sink_enable);
	RESET_FAKE(sm5803_set_vbus_disch);
	RESET_FAKE(sm5803_hibernate);
	RESET_FAKE(sm5803_interrupt);
	RESET_FAKE(sm5803_handle_interrupt);
	RESET_FAKE(sm5803_get_chg_det);

	RESET_FAKE(charger_set_otg_current_voltage);
	RESET_FAKE(charge_set_input_current_limit);
	RESET_FAKE(charger_enable_otg_power);
	RESET_FAKE(charger_is_sourcing_otg_power);
	RESET_FAKE(extpower_handle_update);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(schedule_deferred_pd_interrupt);
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(cbi_get_board_version);
	RESET_FAKE(set_scancode_set2);
	RESET_FAKE(get_scancode_set2);

	/* Make the DB is 1C1A as initial */
	joxer_cached_sub_board = JOXER_SB_C;
	set_fw_config_value(FW_SUB_BOARD_2);
}

ZTEST_SUITE(joxer, NULL, NULL, test_before, NULL, NULL);

static enum ec_error_list sm5803_is_acok_fake_no(int chgnum, bool *acok)
{
	*acok = false;
	return EC_SUCCESS;
}

static enum ec_error_list sm5803_is_acok_fake_yes(int chgnum, bool *acok)
{
	*acok = true;
	return EC_SUCCESS;
}

ZTEST(joxer, test_extpower_is_present)
{
	/* Errors are not-OK */
	sm5803_is_acok_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_false(extpower_is_present());
	zassert_equal(sm5803_is_acok_fake.call_count, 2);

	/* When neither charger is connected, we check both and return no. */
	sm5803_is_acok_fake.custom_fake = sm5803_is_acok_fake_no;
	zassert_false(extpower_is_present());
	zassert_equal(sm5803_is_acok_fake.call_count, 4);

	/* If one is connected, AC is present */
	sm5803_is_acok_fake.custom_fake = sm5803_is_acok_fake_yes;
	zassert_true(extpower_is_present());
	zassert_equal(sm5803_is_acok_fake.call_count, 5);
}

ZTEST(joxer, test_board_check_extpower)
{
	/* Initial state is stable not-present */
	sm5803_is_acok_fake.custom_fake = sm5803_is_acok_fake_no;
	board_check_extpower();
	RESET_FAKE(extpower_handle_update);

	/* Unchanged state does nothing */
	board_check_extpower();
	zassert_equal(extpower_handle_update_fake.call_count, 0);

	/* Changing the state triggers extpower_handle_update() */
	sm5803_is_acok_fake.custom_fake = sm5803_is_acok_fake_yes;
	board_check_extpower();
	zassert_equal(extpower_handle_update_fake.call_count, 1);
}

ZTEST(joxer, test_board_hibernate)
{
	board_hibernate();
	zassert_equal(sm5803_hibernate_fake.call_count, 2);
}

ZTEST(joxer, test_board_vconn_control)
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

ZTEST(joxer, test_pd_check_vbus_level)
{
	/* pd_check_vbus_level delegates directly to sm5803_check_vbus_level */
	pd_check_vbus_level(1, VBUS_PRESENT);
	zassert_equal(sm5803_check_vbus_level_fake.call_count, 1);
	zassert_equal(sm5803_check_vbus_level_fake.arg0_val, 1);
	zassert_equal(sm5803_check_vbus_level_fake.arg1_val, VBUS_PRESENT);
}

ZTEST(joxer, test_chargers_suspend)
{
	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(sm5803_disable_low_power_mode_fake.call_count, 2);

	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(sm5803_enable_low_power_mode_fake.call_count, 2);
}

ZTEST(joxer, test_set_active_charge_port)
{
	/* Asking for an invalid port is an error */
	zassert_equal(board_set_active_charge_port(3), EC_ERROR_INVAL);

	/* A port that's sourcing won't sink */
	charger_is_sourcing_otg_power_fake.return_val = true;
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_INVAL);
	RESET_FAKE(charger_is_sourcing_otg_power);

	/* Enabling a port disables the other one then enables it */
	charge_manager_get_active_charge_port_fake.return_val = 1;
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(sm5803_vbus_sink_enable_fake.call_count, 2);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg0_history[0], 1);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg1_history[0], 0);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg0_history[1], 0);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg1_history[1], 1);
	/* It also temporarily requested discharge on AC */
	zassert_equal(charger_discharge_on_ac_fake.call_count, 2);
	zassert_equal(charger_discharge_on_ac_fake.arg0_history[0], 1);
	zassert_equal(charger_discharge_on_ac_fake.arg0_history[1], 0);
	RESET_FAKE(charger_discharge_on_ac);

	/* Requesting no port skips the enable step */
	RESET_FAKE(sm5803_vbus_sink_enable);
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(sm5803_vbus_sink_enable_fake.call_count, 2);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg0_history[0], 0);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg1_history[0], 0);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg0_history[1], 1);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg1_history[1], 0);

	/* Errors bubble up */
	sm5803_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
}

ZTEST(joxer, test_tcpc_get_alert_status)
{
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl);
	/* GPIO is normally configured by code not tested in this case */
	zassert_ok(gpio_pin_configure_dt(c1_int, GPIO_INPUT));

	tcpci_emul_set_reg(TCPC1, TCPC_REG_ALERT, TCPC_REG_ALERT_CC_STATUS);

	/* Nothing if the IRQ line isn't asserted */
	zassert_ok(gpio_emul_input_set(c1_int->port, c1_int->pin, 1));
	zassert_equal(tcpc_get_alert_status(), 0);

	/* Alert active if it is and the alert register has bits set */
	zassert_ok(gpio_emul_input_set(c1_int->port, c1_int->pin, 0));
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_1);
}

ZTEST(joxer, test_pd_power_supply_reset)
{
	charger_is_sourcing_otg_power_fake.return_val = 1;

	/* Disables sourcing and discharges VBUS on active port */
	pd_power_supply_reset(0);
	zassert_equal(charger_enable_otg_power_fake.call_count, 1);
	zassert_equal(charger_enable_otg_power_fake.arg0_val, 0);
	zassert_equal(charger_enable_otg_power_fake.arg1_val, 0);
	zassert_equal(sm5803_set_vbus_disch_fake.call_count, 1);
	zassert_equal(sm5803_set_vbus_disch_fake.arg0_val, 0);
	zassert_equal(sm5803_set_vbus_disch_fake.arg1_val, 1);

	/* Invalid port does nothing */
	pd_power_supply_reset(2);
	zassert_equal(charger_is_sourcing_otg_power_fake.call_count, 1);
}

ZTEST(joxer, test_pd_set_power_supply_ready)
{
	zassert_ok(pd_set_power_supply_ready(0));
	/* Disabled sinking */
	zassert_equal(sm5803_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(sm5803_vbus_sink_enable_fake.arg0_val, 0);
	zassert_false(sm5803_vbus_sink_enable_fake.arg1_val);
	/* Disabled VBUS discharge */
	zassert_equal(sm5803_set_vbus_disch_fake.call_count, 1);
	zassert_equal(sm5803_set_vbus_disch_fake.arg0_val, 0);
	zassert_false(sm5803_set_vbus_disch_fake.arg1_val);
	/* Enabled sourcing */
	zassert_equal(charger_enable_otg_power_fake.call_count, 1);
	zassert_equal(charger_enable_otg_power_fake.arg0_val, 0);
	zassert_true(charger_enable_otg_power_fake.arg1_val);

	/* Errors cause early return */
	charger_enable_otg_power_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	sm5803_set_vbus_disch_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	sm5803_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	zassert_equal(pd_set_power_supply_ready(31), EC_ERROR_INVAL);
}

ZTEST(joxer, test_typec_set_source_current_limit)
{
	typec_set_source_current_limit(0, TYPEC_RP_3A0);

	zassert_equal(charger_set_otg_current_voltage_fake.call_count, 1);
	zassert_equal(charger_set_otg_current_voltage_fake.arg0_val, 0);
	zassert_equal(charger_set_otg_current_voltage_fake.arg1_val, 3000);
	zassert_equal(charger_set_otg_current_voltage_fake.arg2_val, 5000);

	/* Errors are logged but otherwise ignored */
	charger_set_otg_current_voltage_fake.return_val = EC_ERROR_UNKNOWN;
	typec_set_source_current_limit(1, TYPEC_RP_1A5);
	zassert_equal(charger_set_otg_current_voltage_fake.call_count, 2);
}

ZTEST(joxer, test_typec_set_sink_current_limit)
{
	/* For other case, set 96% charge current limit */
	board_set_charge_limit(0, 1, 3000, 3000, 5000);
	zassert_equal(charge_set_input_current_limit_fake.call_count, 1);
	zassert_equal(charge_set_input_current_limit_fake.arg0_val, 2880);
	zassert_equal(charge_set_input_current_limit_fake.arg1_val, 5000);

	/* For port1 and charge_mv <= 5000, the charge_ma should be 2000 */
	board_set_charge_limit(1, 1, 3000, 3000, 5000);
	zassert_equal(charge_set_input_current_limit_fake.call_count, 2);
	zassert_equal(charge_set_input_current_limit_fake.arg0_val, 2000);
	zassert_equal(charge_set_input_current_limit_fake.arg1_val, 5000);
}

void usb_c0_interrupt(enum gpio_signal unused);

ZTEST(joxer, test_c0_interrupt)
{
	const struct gpio_dt_spec *const c0_irq =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);

	gpio_emul_input_set(c0_irq->port, c0_irq->pin, 0);
	usb_c0_interrupt(0);

	/* Immediately notifies driver tasks */
	zassert_equal(sm5803_interrupt_fake.call_count, 1);
	zassert_equal(sm5803_interrupt_fake.arg0_val, 0);

	/*  De-assert the IRQ */
	gpio_emul_input_set(c0_irq->port, c0_irq->pin, 1);
}

void usb_c1_interrupt(enum gpio_signal unused);

ZTEST(joxer, test_c1_interrupt)
{
	const struct gpio_dt_spec *const c1_irq =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl);

	gpio_emul_input_set(c1_irq->port, c1_irq->pin, 0);
	usb_c1_interrupt(0);

	/* Check if schedule_deferred_pd_interrupt is called */
	zassert_equal(schedule_deferred_pd_interrupt_fake.call_count, 1);
	zassert_equal(schedule_deferred_pd_interrupt_fake.arg0_val, 1);
	/*  De-assert the IRQ */
	gpio_emul_input_set(c1_irq->port, c1_irq->pin, 1);
}

void board_handle_initial_typec_irq(void);

ZTEST(joxer, test_board_handle_initial_typec_irq)
{
	const struct gpio_dt_spec *const c0_irq =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);

	gpio_emul_input_set(c0_irq->port, c0_irq->pin, 0);
	board_handle_initial_typec_irq();

	/* Immediately notifies driver tasks */
	zassert_equal(sm5803_interrupt_fake.call_count, 1);
	zassert_equal(sm5803_interrupt_fake.arg0_val, 0);

	/*  De-assert the IRQ */
	gpio_emul_input_set(c0_irq->port, c0_irq->pin, 1);
}

ZTEST(joxer, test_board_process_pd_alert)
{
	const struct gpio_dt_spec *const c1_irq =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl);

	gpio_emul_input_set(c1_irq->port, c1_irq->pin, 0);
	board_process_pd_alert(1);
	zassert_equal(sm5803_handle_interrupt_fake.call_count, 1);
	zassert_equal(sm5803_handle_interrupt_fake.arg0_val, 1);

	/* Does nothing if IRQ is not asserted */
	gpio_emul_input_set(c1_irq->port, c1_irq->pin, 1);
	board_process_pd_alert(1);
	zassert_equal(sm5803_handle_interrupt_fake.call_count, 1);

	/* Does nothing for port 0 */
	board_process_pd_alert(0);
	zassert_equal(sm5803_handle_interrupt_fake.call_count, 1);
}

static enum ec_error_list sm5803_get_chg_det_present(int port, int *present)
{
	*present = 1;
	return EC_SUCCESS;
}

ZTEST(joxer, test_pd_snk_is_vbus_provided)
{
	/* pd_snk_is_vbus_provided just delegates to sm5803_get_chg_det */
	sm5803_get_chg_det_fake.custom_fake = sm5803_get_chg_det_present;
	zassert_true(pd_snk_is_vbus_provided(0));
	zassert_equal(sm5803_get_chg_det_fake.call_count, 1);
	zassert_equal(sm5803_get_chg_det_fake.arg0_val, 0);
}

static int keyboard_layout;

static int cros_cbi_get_fw_config_mock(enum cbi_fw_config_field_id field_id,
				       uint32_t *value)
{
	if (field_id != FW_KB_FEATURE)
		return -EINVAL;

	switch (keyboard_layout) {
	case 0:
		*value = FW_KB_FEATURE_BL_ABSENT_DEFAULT;
		break;
	case 1:
		*value = FW_KB_FEATURE_BL_ABSENT_US2;
		break;
	case 2:
		*value = FW_KB_FEATURE_BL_PRESENT_DEFAULT;
		break;
	case 3:
		*value = FW_KB_FEATURE_BL_PRESENT_US2;
		break;
	default:
		return 0;
	}
	return 0;
}

ZTEST(joxer, test_kb_layout_init)
{
	cros_cbi_get_fw_config_fake.custom_fake = cros_cbi_get_fw_config_mock;

	keyboard_layout = 0;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 0);
	zassert_equal(get_scancode_set2_fake.call_count, 0);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &joxer_kb_wo_kb_light);

	keyboard_layout = 2;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 0);
	zassert_equal(get_scancode_set2_fake.call_count, 0);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &joxer_kb_w_kb_light);

	keyboard_layout = 1;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 1);
	zassert_equal(get_scancode_set2_fake.call_count, 1);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &joxer_kb_wo_kb_light);

	keyboard_layout = 3;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 2);
	zassert_equal(get_scancode_set2_fake.call_count, 2);
	zassert_equal_ptr(board_vivaldi_keybd_config(), &joxer_kb_w_kb_light);
}

ZTEST(joxer, test_kb_layout_init_cbi_error)
{
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	kb_layout_init();
	zassert_equal(set_scancode_set2_fake.call_count, 0);
	zassert_equal(get_scancode_set2_fake.call_count, 0);
}

static int get_fan_config_present(enum cbi_fw_config_field_id field,
				  uint32_t *value)
{
	zassert_equal(field, FW_FAN);
	*value = FW_FAN_PRESENT;
	return 0;
}

static int get_fan_config_absent(enum cbi_fw_config_field_id field,
				 uint32_t *value)
{
	zassert_equal(field, FW_FAN);
	*value = FW_FAN_NOT_PRESENT;
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

ZTEST(joxer, test_fan_present)
{
	int flags;

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_present;
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;
	fan_init();

	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW,
		      "actual GPIO flags were %#x", flags);
}

ZTEST(joxer, test_fan_absent)
{
	int flags;
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
			      GPIO_DISCONNECTED);

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_absent;
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;
	fan_init();

	zassert_equal(fan_get_count(), 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);
}

ZTEST(joxer, test_fan_config)
{
	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_present;
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;
	fan_init();

	zassert_equal_ptr(fan_config[0].tach,
			  DEVICE_DT_GET(DT_NODELABEL(tach1)),
			  "fan_config should not change");

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_present;
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;
	fan_init();

	zassert_equal_ptr(
		fan_config[0].tach, DEVICE_DT_GET(DT_NODELABEL(tach0)),
		"fan_config should change to tach0 if board version > 1");
}

ZTEST(joxer, test_fan_cbi_error)
{
	int flags;
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
			      GPIO_DISCONNECTED);

	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	fan_init();

	zassert_equal(fan_get_count(), 0);
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(gpio_fan_enable), &flags));
	zassert_equal(flags, 0, "actual GPIO flags were %#x", flags);

	cros_cbi_get_fw_config_fake.custom_fake = get_fan_config_present;
	cbi_get_board_version_fake.return_val = EINVAL;
	fan_init();

	zassert_equal_ptr(fan_config[0].tach,
			  DEVICE_DT_GET(DT_NODELABEL(tach1)),
			  "fan_config should not change");
}

static int get_base_orientation_normal(enum cbi_fw_config_field_id field,
				       uint32_t *value)
{
	zassert_equal(field, FW_LID_INVERSION);
	*value = SENSOR_DEFAULT;
	return 0;
}

static int get_base_orientation_inverted(enum cbi_fw_config_field_id field,
					 uint32_t *value)
{
	zassert_equal(field, FW_LID_INVERSION);
	*value = SENSOR_INVERTED;
	return 0;
}

ZTEST(joxer, test_lid_sensor_inversion)
{
	const void *const normal_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_ref));
	const void *const inverted_rotation =
		&SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_inverted));

	/*
	 * Normally this gets set to rot-standard-ref during other init,
	 * which we aren't running in this test.
	 */
	motion_sensors[LID_ACCEL].rot_standard_ref = normal_rotation;

	cros_cbi_get_fw_config_fake.custom_fake = get_base_orientation_normal;
	form_factor_init();
	zassert_equal_ptr(
		motion_sensors[LID_ACCEL].rot_standard_ref, normal_rotation,
		"normal orientation should use the standard rotation matrix");

	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.return_val = EINVAL;
	form_factor_init();
	zassert_equal_ptr(motion_sensors[LID_ACCEL].rot_standard_ref,
			  normal_rotation,
			  "errors should leave the rotation unchanged");

	cros_cbi_get_fw_config_fake.custom_fake = get_base_orientation_inverted;
	form_factor_init();
	zassert_equal_ptr(
		motion_sensors[LID_ACCEL].rot_standard_ref, inverted_rotation,
		"inverted orientation should use the inverted rotation matrix");
}

enum joxer_sub_board_type joxer_get_sb_type(void);

static int
get_fake_sub_board_fw_config_field(enum cbi_fw_config_field_id field_id,
				   uint32_t *value)
{
	*value = fw_config_value;
	return 0;
}

int init_gpios(const struct device *unused);

ZTEST(joxer, test_db_without_c_a)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	/* Reset cached global state. */
	joxer_cached_sub_board = JOXER_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_1);
	zassert_equal(joxer_get_sb_type(), JOXER_SB);
	zassert_equal(board_get_usb_pd_port_count(), 1);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl),
			  GPIO_PULL_UP | GPIO_INPUT);

	joxer_cached_sub_board = JOXER_SB_C;
	fw_config_value = -1;
}

ZTEST(joxer, test_db_with_c)
{
	cros_cbi_get_fw_config_fake.custom_fake =
		get_fake_sub_board_fw_config_field;
	/* Reset cached global state. */
	joxer_cached_sub_board = JOXER_SB_UNKNOWN;
	fw_config_value = -1;

	/* Set the sub-board, reported configuration is correct. */
	set_fw_config_value(FW_SUB_BOARD_2);
	zassert_equal(joxer_get_sb_type(), JOXER_SB_C);
	zassert_equal(board_get_usb_pd_port_count(), 2);

	init_gpios(NULL);
	hook_notify(HOOK_INIT);

	ASSERT_GPIO_FLAGS(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl),
			  GPIO_PULL_UP | GPIO_INPUT | GPIO_INT_EDGE_FALLING);

	joxer_cached_sub_board = JOXER_SB_C;
	fw_config_value = -1;
}

static int get_fw_config_error(enum cbi_fw_config_field_id field,
			       uint32_t *value)
{
	return EC_ERROR_UNKNOWN;
}

ZTEST(joxer, test_cbi_error)
{
	/*
	 * Reading fw_config from CBI returns an error, so sub-board is treated
	 * as unknown.
	 */
	joxer_cached_sub_board = JOXER_SB_UNKNOWN;
	fw_config_value = -1;

	cros_cbi_get_fw_config_fake.custom_fake = get_fw_config_error;
	zassert_equal(joxer_get_sb_type(), JOXER_SB_UNKNOWN);
}
