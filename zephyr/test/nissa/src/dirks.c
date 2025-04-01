/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "ap_power/ap_power_events.h"
#include "board_led.h"
#include "charge_manager.h"
#include "chipset.h"
#include "dirks.h"
#include "driver/retimer/ps8811.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "led_common.h"
#include "nissa_hdmi.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "typec_control.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

extern const struct ec_response_keybd_config nereid_kb_legacy;

FAKE_VOID_FUNC(nissa_configure_hdmi_rails);
FAKE_VOID_FUNC(nissa_configure_hdmi_vcc);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);

FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_is_vbus_present, int);
FAKE_VOID_FUNC(syv682x_interrupt, enum gpio_signal)

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, set_color_power, enum led_color, int);

FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);
FAKE_VOID_FUNC(charge_manager_update_charge, int, int,
	       const struct charge_port_info *);
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port);
FAKE_VALUE_FUNC(enum charge_supplier, charge_manager_get_supplier);
FAKE_VOID_FUNC(typec_set_input_current_limit, int, typec_current_t, uint32_t);
FAKE_VALUE_FUNC(int, charge_manager_get_power_limit_uw);
FAKE_VALUE_FUNC(int, charge_manager_get_charger_voltage);
FAKE_VALUE_FUNC(int, ppc_set_vbus_source_current_limit, int,
		enum tcpc_rp_value);
FAKE_VALUE_FUNC(int, adc_read_ppvar_pwr_in_imon);
FAKE_VALUE_FUNC(int, ps8811_i2c_write, const struct usb_mux *, int, int, int);

uint8_t board_get_charger_chip_count(void)
{
	return 2;
}

static void test_before(void *fixture)
{
	RESET_FAKE(nissa_configure_hdmi_rails);
	RESET_FAKE(nissa_configure_hdmi_vcc);
	RESET_FAKE(cbi_get_board_version);

	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(ppc_is_vbus_present);
	RESET_FAKE(syv682x_interrupt);

	RESET_FAKE(chipset_in_state);
	RESET_FAKE(set_color_power);

	RESET_FAKE(adc_read_channel);
	RESET_FAKE(charge_manager_update_charge);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(charge_manager_get_supplier);
	RESET_FAKE(typec_set_input_current_limit);
	RESET_FAKE(charge_manager_get_power_limit_uw);
	RESET_FAKE(charge_manager_get_charger_voltage);
	RESET_FAKE(ppc_set_vbus_source_current_limit);
	RESET_FAKE(adc_read_ppvar_pwr_in_imon);
	RESET_FAKE(ps8811_i2c_write);
}

ZTEST_SUITE(dirks, NULL, NULL, test_before, NULL, NULL);

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

ZTEST(dirks, test_hdmi_power)
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

ZTEST(dirks, test_extpower_is_present)
{
	/* AC is always present */
	zassert_true(extpower_is_present());
}

ZTEST(dirks, test_ppc_interrupt)
{
	const struct gpio_dt_spec *usb_c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_fault_l);

	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set(usb_c0_int->port, usb_c0_int->pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(usb_c0_int->port, usb_c0_int->pin, 0),
		   NULL);
	zassert_equal(syv682x_interrupt_fake.call_count, 1);
}

ZTEST(dirks, test_board_vbus_source_enabled)
{
	/* check C0 */
	board_vbus_source_enabled(0);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);

	/* Errors cause early return */
	ppc_is_sourcing_vbus_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_vbus_source_enabled(0), EC_ERROR_UNKNOWN);

	/* Invalid port always return 0 */
	zassert_equal(board_vbus_source_enabled(1), 0);
}

ZTEST(dirks, test_pd_power_supply_reset)
{
	ppc_is_sourcing_vbus_fake.return_val = 1;

	/* Disables sourcing and discharges VBUS on active port */
	pd_power_supply_reset(0);
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 0);
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
	zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
	zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 1);

	/* Invalid port does nothing */
	pd_power_supply_reset(1);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);
}

ZTEST(dirks, test_pd_set_power_supply_ready)
{
	zassert_ok(pd_set_power_supply_ready(0));
	/* Disable charging */
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	zassert_false(ppc_vbus_sink_enable_fake.arg1_val);
	/* Disabled VBUS discharge */
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
	zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
	zassert_false(pd_set_vbus_discharge_fake.arg1_val);
	/* Enabled sourcing */
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_true(ppc_vbus_source_enable_fake.arg1_val);

	/* Errors cause early return */
	ppc_vbus_source_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	zassert_equal(pd_set_power_supply_ready(31), EC_ERROR_INVAL);
}

ZTEST(dirks, test_pd_snk_is_vbus_provided)
{
	/* pd_snk_is_vbus_provided just delegates to ppc_is_vbus_present */
	pd_snk_is_vbus_provided(0);
	zassert_equal(ppc_is_vbus_present_fake.call_count, 1);

	/* Invalid port */
	zassert_equal(pd_snk_is_vbus_provided(1), 0);
}

#define TEST_DELAY_MS 1
#define LED_CPU_DELAY_MS (2000 + TEST_DELAY_MS)
#define LED_PULSE_TICK_US 40
#define LED_PULSE_US 2000

static int sys_state;

static int chipset_in_state_mock(int state_mask)
{
	return (state_mask & sys_state) == sys_state;
}

ZTEST(dirks, test_led_shutdown)
{
	/* LED off when shutdown */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(set_color_power_fake.call_count, 0);
	k_sleep(K_MSEC(LED_CPU_DELAY_MS));
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_OFF);
	zassert_equal(set_color_power_fake.arg1_val, 0);
}

ZTEST(dirks, test_led_suspend)
{
	/* WHITE LED breathing when suspend */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(set_color_power_fake.call_count, 0);
	k_sleep(K_MSEC(LED_CPU_DELAY_MS));
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 0);
	k_sleep(K_MSEC(LED_PULSE_TICK_US));
	zassert_equal(set_color_power_fake.call_count, 2);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 2);
	k_sleep(K_MSEC(LED_PULSE_TICK_US));
	zassert_equal(set_color_power_fake.call_count, 3);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 4);
}

ZTEST(dirks, test_led_resume)
{
	/* WHITE LED always on when chipset is on */
	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 100);
}

ZTEST(dirks, test_led_init)
{
	/* If chipset state is off at init, Turn white led off */
	sys_state = CHIPSET_STATE_ANY_OFF;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;

	hook_notify(HOOK_INIT);
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 0);

	/* If chipset state is on at init, Turn white led on */
	sys_state = CHIPSET_STATE_ON;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;

	hook_notify(HOOK_INIT);
	zassert_equal(set_color_power_fake.call_count, 2);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 100);
}

ZTEST(dirks, test_led_brightness_range)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { 0 };

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_OFF);
	zassert_equal(set_color_power_fake.arg1_val, 0);

	/* Verify LED colors defined are reflected in the brightness
	 * array.
	 */
	led_get_brightness_range(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_RED], 100);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 100);

	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(set_color_power_fake.call_count, 2);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 100);

	memset(brightness, 0, sizeof(brightness));

	brightness[EC_LED_COLOR_RED] = 50;
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(set_color_power_fake.call_count, 3);
	zassert_equal(set_color_power_fake.arg0_val, LED_RED);
	zassert_equal(set_color_power_fake.arg1_val, 50);
}

ZTEST(dirks, test_board_get_vbus_voltage)
{
	board_get_vbus_voltage(0);
	zassert_equal(adc_read_channel_fake.call_count, 1);
	zassert_equal(adc_read_channel_fake.arg0_val, ADC_VBUS);
}

#define ADP_DEBOUNCE_MS \
	(1000 + TEST_DELAY_MS) /* Debounce time for BJ plug/unplug */

struct charge_port_info port_info = {
	.current = 0,
	.voltage = 0,
};

static void
mock_charge_manager_update_charge(int port, int en,
				  const struct charge_port_info *info)
{
	port_info.current = info->current;
	port_info.voltage = info->voltage;
}

ZTEST(dirks, test_adp_connect_interrupt)
{
	const struct gpio_dt_spec *bj_adp_present =
		GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present);

	charge_manager_update_charge_fake.custom_fake =
		mock_charge_manager_update_charge;

	board_bj_init();

	/* when bj adp is present, charge current is 3420mA and charge voltage
	 * is 19V */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 1),
		   NULL);
	k_sleep(K_MSEC(ADP_DEBOUNCE_MS));
	zassert_equal(3420, port_info.current, "port current:%d",
		      port_info.current);
	zassert_equal(19000, port_info.voltage, "port voltage:%d",
		      port_info.voltage);

	/* when bj adp isn't present, charge current is 0mA and charge voltage
	 * is 0V */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 0),
		   NULL);
	k_sleep(K_MSEC(ADP_DEBOUNCE_MS));
	zassert_equal(0, port_info.current, "port current:%d",
		      port_info.current);
	zassert_equal(0, port_info.voltage, "port voltage:%d",
		      port_info.voltage);
}

ZTEST(dirks, test_board_set_active_charge_port)
{
	const struct gpio_dt_spec *bj_adp_present =
		GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present);
	const struct gpio_dt_spec *en_ppvar =
		GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_bj_adp_od);
	int port;

	/* clear call_count */
	charge_manager_get_active_charge_port_fake.call_count = 0;
	board_bj_init();

	/* invalid port */
	port = CHARGE_PORT_COUNT;
	zassert_equal(board_set_active_charge_port(port), EC_ERROR_INVAL);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count, 1);

	/* set to bj adp port when chipset state is on. */
	port = 1;
	zassert_equal(gpio_emul_output_get(en_ppvar->port, en_ppvar->pin), 0);
	sys_state = CHIPSET_STATE_ON;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	/* the active charge port isn't CHARGE_PORT_NONE */
	charge_manager_get_active_charge_port_fake.return_val = 0;
	zassert_equal(board_set_active_charge_port(port), EC_ERROR_INVAL);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count, 3);
	/* bj adp is not present. */
	charge_manager_get_active_charge_port_fake.return_val =
		CHARGE_PORT_NONE;
	zassert_equal(board_set_active_charge_port(port), EC_ERROR_INVAL);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count, 5);
	/* bj adp is present. */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 1),
		   NULL);
	k_sleep(K_MSEC(ADP_DEBOUNCE_MS));
	zassert_equal(board_set_active_charge_port(port), EC_SUCCESS);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count, 7);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	zassert_false(ppc_vbus_sink_enable_fake.arg1_val);
	zassert_equal(gpio_emul_output_get(en_ppvar->port, en_ppvar->pin), 1);

	/* charge port is set to bj adp so the active port is 1. */
	charge_manager_get_active_charge_port_fake.return_val = port;

	/* set to typec port. */
	port = 0;
	/* ppc_is_sourcing_vbus failed. */
	ppc_is_sourcing_vbus_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_set_active_charge_port(port), EC_ERROR_INVAL);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count, 8);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);
	/* ppc_is_sourcing_vbus is pass but active port is not CHARGE_PORT_NONE.
	 * We can't set new charge port when chipset is on.
	 */
	ppc_is_sourcing_vbus_fake.return_val = EC_SUCCESS;
	zassert_equal(board_set_active_charge_port(port), EC_ERROR_INVAL);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count,
		      10);
	/* system off and unplugged bj adp. */
	sys_state = CHIPSET_STATE_ANY_OFF;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 0),
		   NULL);
	k_sleep(K_MSEC(ADP_DEBOUNCE_MS));
	zassert_equal(board_set_active_charge_port(port), EC_SUCCESS);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count,
		      11);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 2);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	zassert_true(ppc_vbus_sink_enable_fake.arg1_val);
	zassert_equal(gpio_emul_output_get(en_ppvar->port, en_ppvar->pin), 0);

	/* charge port is set to typec so the active port is 0. */
	charge_manager_get_active_charge_port_fake.return_val = port;

	/* active port not changed */
	zassert_equal(board_set_active_charge_port(port), EC_SUCCESS);
	zassert_equal(charge_manager_get_active_charge_port_fake.call_count,
		      12);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 2);
	zassert_equal(gpio_emul_output_get(en_ppvar->port, en_ppvar->pin), 0);
}

ZTEST(dirks, test_board_charge_manager_init)
{
	const struct gpio_dt_spec *bj_adp_present =
		GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present);

	/* bj adp present pin is high at the beginning. */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 1),
		   NULL);

	/* powered by bj adapter */
	hook_notify(HOOK_INIT);
	zassert_equal(charge_manager_update_charge_fake.call_count, 10);
	zassert_equal(charge_manager_update_charge_fake.arg0_val,
		      CHARGE_SUPPLIER_DEDICATED);
	zassert_equal(charge_manager_update_charge_fake.arg1_val,
		      DEDICATED_CHARGE_PORT);
	zassert_equal(typec_set_input_current_limit_fake.call_count, 0);

	/* bj adp present pin is low at the beginning. */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 0),
		   NULL);

	/* powered by typec0 */
	hook_notify(HOOK_INIT);
	zassert_equal(charge_manager_update_charge_fake.call_count, 19);
	zassert_equal(typec_set_input_current_limit_fake.call_count, 1);
	zassert_equal(typec_set_input_current_limit_fake.arg0_val, 0);
	zassert_equal(typec_set_input_current_limit_fake.arg1_val, 3000);
	zassert_equal(typec_set_input_current_limit_fake.arg2_val, 5000);
}

#define POWER_DELAY_MS 2

ZTEST(dirks, test_power_monitor)
{
	/* If CPU is off or suspended, no need to throttle or restrict power. */
	sys_state = CHIPSET_STATE_ANY_OFF;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	hook_notify(HOOK_INIT);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 0);
	k_sleep(K_MSEC(20 + TEST_DELAY_MS));
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 0);

	/* CPU is on */
	sys_state = CHIPSET_STATE_ON;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	/* If the gap is less than THROT_LOW_THRESHOLD, start throttling. */
	charge_manager_get_power_limit_uw_fake.return_val = 3250 * 20000;
	charge_manager_get_charger_voltage_fake.return_val = 18800;
	adc_read_ppvar_pwr_in_imon_fake.return_val = 3250;
	ppc_is_sourcing_vbus_fake.return_val = 1;
	hook_notify(HOOK_INIT);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 1);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg1_val,
		      TYPEC_RP_1A5);
	/* If the gap is greater than THROT_HIGH_THRESHOLD + POWER_GAIN_TYPE_C,
	 * stop throttling.
	 */
	charge_manager_get_charger_voltage_fake.return_val = 14800;
	k_sleep(K_MSEC(20 * 10));
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 2);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg1_val,
		      TYPEC_RP_3A0);
}

static int vbus_in;

static int adc_read_channel_mock(enum adc_channel ch)
{
	return vbus_in;
}

static int active_port;

static int charge_manager_get_active_charge_port_mock(void)
{
	return active_port;
}

ZTEST(dirks, test_board_is_power_good)
{
	const struct gpio_dt_spec *bj_adp_present =
		GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present);

	/* powered by BJ adp */
	active_port = CHARGE_PORT_BARRELJACK;
	charge_manager_get_active_charge_port_fake.custom_fake =
		charge_manager_get_active_charge_port_mock;
	/* bj adp present pin is exist */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 1),
		   NULL);
	zassert_true(board_is_power_good());

	/* bj adp present pin is not exist */
	zassert_ok(gpio_emul_input_set(bj_adp_present->port,
				       bj_adp_present->pin, 0),
		   NULL);
	zassert_false(board_is_power_good());

	/* powered by USBC adapter */
	active_port = CHARGE_PORT_TYPEC0;
	charge_manager_get_active_charge_port_fake.custom_fake =
		charge_manager_get_active_charge_port_mock;

	/* the request vbus is 20V, actual vbus is 19010 mV
	 * which is more than 5% less (19000mV).
	 */
	charge_manager_get_charger_voltage_fake.return_val = 20000;
	vbus_in = 19010;
	adc_read_channel_fake.custom_fake = adc_read_channel_mock;
	zassert_true(board_is_power_good());

	/* vbus is less than 19000mV */
	vbus_in = 18900;
	adc_read_channel_fake.custom_fake = adc_read_channel_mock;
	zassert_false(board_is_power_good());

	/* charge port is not BJ or USBC */
	active_port = -1;
	charge_manager_get_active_charge_port_fake.custom_fake =
		charge_manager_get_active_charge_port_mock;
	zassert_false(board_is_power_good());
}

ZTEST(dirks, test_usba_retimer_init)
{
	hook_notify(HOOK_CHIPSET_STARTUP);

	zassert_equal(ps8811_i2c_write_fake.call_count, 1);
	zassert_equal(ps8811_i2c_write_fake.arg1_val, PS8811_REG_PAGE1);
	zassert_equal(ps8811_i2c_write_fake.arg2_val,
		      PS8811_REG1_USB_CHAN_A_SWING);
	zassert_equal(ps8811_i2c_write_fake.arg3_val, 0x20);
}
