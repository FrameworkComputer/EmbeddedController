/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "charge_manager.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "keyboard_protocol.h"
#include "nissa_hdmi.h"
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

extern const struct ec_response_keybd_config nereid_kb_legacy;

FAKE_VOID_FUNC(nissa_configure_hdmi_rails);
FAKE_VOID_FUNC(nissa_configure_hdmi_vcc);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);

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
FAKE_VALUE_FUNC(enum ec_error_list, charger_enable_otg_power, int, int);
FAKE_VALUE_FUNC(int, charger_is_sourcing_otg_power, int);
FAKE_VOID_FUNC(extpower_handle_update, int);
FAKE_VOID_FUNC(charger_discharge_on_ac, int);
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port);
FAKE_VOID_FUNC(usb_charger_task_set_event, int, uint8_t);
FAKE_VOID_FUNC(usb_charger_task_set_event_sync, int, uint8_t);

uint8_t board_get_charger_chip_count(void)
{
	return 2;
}

static void test_before(void *fixture)
{
	RESET_FAKE(nissa_configure_hdmi_rails);
	RESET_FAKE(nissa_configure_hdmi_vcc);
	RESET_FAKE(cbi_get_board_version);

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
	RESET_FAKE(charger_enable_otg_power);
	RESET_FAKE(charger_is_sourcing_otg_power);
	RESET_FAKE(extpower_handle_update);
	RESET_FAKE(charger_discharge_on_ac);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(usb_charger_task_set_event);
	RESET_FAKE(usb_charger_task_set_event_sync);
}

ZTEST_SUITE(nereid, NULL, NULL, test_before, NULL, NULL);

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

ZTEST(nereid, test_hdmi_power)
{
	/* Board version less than 2 configures both */
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;
	nissa_configure_hdmi_power_gpios();
	zassert_equal(nissa_configure_hdmi_vcc_fake.call_count, 1);
	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 1);

	/* Later versions only enable core rails */
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;
	nissa_configure_hdmi_power_gpios();
	zassert_equal(nissa_configure_hdmi_vcc_fake.call_count, 1);
	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 2);
}

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

ZTEST(nereid, test_extpower_is_present)
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

ZTEST(nereid, test_board_check_extpower)
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

ZTEST(nereid, test_board_hibernate)
{
	board_hibernate();
	zassert_equal(sm5803_hibernate_fake.call_count, 2);
}

ZTEST(nereid, test_board_vconn_control)
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

ZTEST(nereid, test_pd_check_vbus_level)
{
	/* pd_check_vbus_level delegates directly to sm5803_check_vbus_level */
	pd_check_vbus_level(1, VBUS_PRESENT);
	zassert_equal(sm5803_check_vbus_level_fake.call_count, 1);
	zassert_equal(sm5803_check_vbus_level_fake.arg0_val, 1);
	zassert_equal(sm5803_check_vbus_level_fake.arg1_val, VBUS_PRESENT);
}

ZTEST(nereid, test_chargers_suspend)
{
	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(sm5803_disable_low_power_mode_fake.call_count, 2);

	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(sm5803_enable_low_power_mode_fake.call_count, 2);
}

ZTEST(nereid, test_set_active_charge_port)
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

ZTEST(nereid, test_tcpc_get_alert_status)
{
	const struct gpio_dt_spec *c1_int =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);
	/* GPIO is normally configured by code not tested in this case */
	zassert_ok(gpio_pin_configure_dt(c1_int, GPIO_INPUT));

	tcpci_emul_set_reg(EMUL_DT_GET(DT_NODELABEL(tcpci_emul_1)),
			   TCPC_REG_ALERT, TCPC_REG_ALERT_CC_STATUS);

	/* Nothing if the IRQ line isn't asserted */
	zassert_ok(gpio_emul_input_set(c1_int->port, c1_int->pin, 1));
	zassert_equal(tcpc_get_alert_status(), 0);

	/* Alert active if it is and the alert register has bits set */
	zassert_ok(gpio_emul_input_set(c1_int->port, c1_int->pin, 0));
	zassert_equal(tcpc_get_alert_status(), PD_STATUS_TCPC_ALERT_1);
}

ZTEST(nereid, test_pd_power_supply_reset)
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

ZTEST(nereid, test_pd_set_power_supply_ready)
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

ZTEST(nereid, test_typec_set_source_current_limit)
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

void usb_c0_interrupt(enum gpio_signal unused);

ZTEST(nereid, test_c0_interrupt)
{
	const struct gpio_dt_spec *const c0_irq =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);

	gpio_emul_input_set(c0_irq->port, c0_irq->pin, 0);
	usb_c0_interrupt(0);

	/* Immediately notifies driver tasks */
	zassert_equal(usb_charger_task_set_event_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_fake.arg0_val, 0);
	zassert_equal(usb_charger_task_set_event_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
	zassert_equal(sm5803_interrupt_fake.call_count, 1);
	zassert_equal(sm5803_interrupt_fake.arg0_val, 0);

	/*
	 * Notifies again 5ms later if the IRQ is still asserted. It may take
	 * more than 5ms to actually run the handler, so only ensure that it
	 * gets run at least once in 100ms (which seems reliable).
	 */
	k_sleep(K_MSEC(100));
	zassert_equal(sm5803_interrupt_fake.call_count,
		      usb_charger_task_set_event_fake.call_count);
	zassert_true(usb_charger_task_set_event_fake.call_count > 1,
		     "handlers were notified %d time(s)",
		     usb_charger_task_set_event_fake.call_count);

	/*
	 * Stops notifying once the IRQ is deasserted, even if
	 * polls were pending.
	 */
	unsigned int notify_count = sm5803_interrupt_fake.call_count;

	gpio_emul_input_set(c0_irq->port, c0_irq->pin, 1);
	k_sleep(K_MSEC(100));
	zassert_equal(usb_charger_task_set_event_fake.call_count, notify_count);
	zassert_equal(sm5803_interrupt_fake.call_count,
		      usb_charger_task_set_event_fake.call_count);
}

ZTEST(nereid, test_usb_c1_interrupt)
{
	const struct gpio_dt_spec *const c1_irq =
		GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);

	gpio_emul_input_set(c1_irq->port, c1_irq->pin, 0);
	board_process_pd_alert(1);
	zassert_equal(sm5803_handle_interrupt_fake.call_count, 1);
	zassert_equal(sm5803_handle_interrupt_fake.arg0_val, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.arg0_val, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.arg1_val,
		      USB_CHG_EVENT_BC12);

	/* Does nothing if IRQ is not asserted */
	gpio_emul_input_set(c1_irq->port, c1_irq->pin, 1);
	board_process_pd_alert(1);
	zassert_equal(sm5803_handle_interrupt_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.call_count, 1);

	/* Does nothing for port 0 */
	board_process_pd_alert(0);
	zassert_equal(sm5803_handle_interrupt_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_sync_fake.call_count, 1);
}

static enum ec_error_list sm5803_get_chg_det_present(int port, int *present)
{
	*present = 1;
	return EC_SUCCESS;
}

ZTEST(nereid, test_pd_snk_is_vbus_provided)
{
	/* pd_snk_is_vbus_provided just delegates to sm5803_get_chg_det */
	sm5803_get_chg_det_fake.custom_fake = sm5803_get_chg_det_present;
	zassert_true(pd_snk_is_vbus_provided(0));
	zassert_equal(sm5803_get_chg_det_fake.call_count, 1);
	zassert_equal(sm5803_get_chg_det_fake.arg0_val, 0);
}
