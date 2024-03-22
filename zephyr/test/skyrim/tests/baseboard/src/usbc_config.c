/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_ramp.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "emul/tcpc/emul_nct38xx.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ioexpander.h"
#include "power.h"
#include "system.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_flags.h"
#include "usbc_config.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define IOEX_GPIO_COUNT 8

/* Mocks and various functions needed for the tests. */
void reset_nct38xx_port(int port);
int nct38xx_tcpm_init(int port);

FAKE_VOID_FUNC(pd_handle_overcurrent, int)
FAKE_VOID_FUNC(usb_charger_task_set_event, int, uint8_t)
FAKE_VOID_FUNC(battery_sleep_fuel_gauge)
FAKE_VALUE_FUNC(int, charge_manager_get_active_charge_port)
FAKE_VOID_FUNC(pd_request_source_voltage, int, int)
FAKE_VALUE_FUNC(enum ec_error_list, charger_get_vbus_voltage, int, int *)
FAKE_VALUE_FUNC(int, usb_charge_set_mode, int, enum usb_charge_mode,
		enum usb_suspend_charge)
FAKE_VOID_FUNC(pd_set_error_recovery, int)
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int)
FAKE_VALUE_FUNC(bool, pd_is_battery_capable)

struct reset_toggle_info {
	int call_count;
	uint64_t us;
} nct38xx_reset_toggles[CONFIG_USB_PD_PORT_MAX_COUNT];

bool ppc_vbus_sink_enable_enabled[CONFIG_USB_PD_PORT_MAX_COUNT];
int pd_set_error_recovery_call_count[CONFIG_USB_PD_PORT_MAX_COUNT];

static void pd_set_error_recovery_mock(int port)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	pd_set_error_recovery_call_count[port]++;
}

static int ppc_vbus_sink_enable_mock(int port, int enable)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return -EINVAL;

	ppc_vbus_sink_enable_enabled[port] = enable;
	return 0;
}

void test_nct38xx0_interrupt(enum gpio_signal signal)
{
	const struct gpio_dt_spec *reset_gpio_l = &tcpc_config[0].rst_gpio;

	nct38xx_reset_toggles[0].call_count++;
	if (gpio_pin_get_dt(reset_gpio_l))
		nct38xx_reset_toggles[0].us = get_time().val;
	else
		nct38xx_reset_toggles[0].us =
			nct38xx_reset_toggles[0].us - get_time().val;
}

void test_nct38xx1_interrupt(enum gpio_signal signal)
{
	const struct gpio_dt_spec *reset_gpio_l = &tcpc_config[1].rst_gpio;

	nct38xx_reset_toggles[1].call_count++;
	if (gpio_pin_get_dt(reset_gpio_l))
		nct38xx_reset_toggles[1].us = get_time().val;
	else
		nct38xx_reset_toggles[1].us =
			nct38xx_reset_toggles[1].us - get_time().val;
}

int pd_get_retry_count(int port, enum tcpci_msg_type type)
{
	return 3;
}

void pd_transmit_complete(int port, int status)
{
}

enum usb_pd_vbus_detect get_usb_pd_vbus_detect(void)
{
	return 0;
}

void pd_set_suspend(int port, int suspend)
{
}

void pd_deferred_resume(int port)
{
}

void pd_vbus_low(int port)
{
}

int board_is_sourcing_vbus(int port)
{
	return 0;
}

void charge_manager_update_charge(int supplier, int port,
				  const struct charge_port_info *charge)
{
}

int pd_is_vbus_present(int port)
{
	return 0;
}

uint8_t board_get_usb_pd_port_count(void)
{
	return 2;
}

void schedule_deferred_pd_interrupt(int port)
{
}

void pd_got_frs_signal(int port)
{
}

static int mock_voltage;
static enum ec_error_list charger_get_vbus_voltage_mock(int port, int *voltage)
{
	*voltage = mock_voltage;
	return 0;
}

/* Helper functions for tests. */
static int gpio_emul_output_get_dt(const struct gpio_dt_spec *dt)
{
	return gpio_emul_output_get(dt->port, dt->pin);
}

static int gpio_emul_input_set_dt(const struct gpio_dt_spec *dt, int value)
{
	return gpio_emul_input_set(dt->port, dt->pin, value);
}

static int toggle_pin_falling(const struct gpio_dt_spec *dt)
{
	int rv;

	rv = gpio_emul_input_set_dt(dt, 1);
	if (rv)
		return rv;

	rv = gpio_emul_input_set_dt(dt, 0);
	if (rv)
		return rv;

	return 0;
}

static int toggle_pin_rising(const struct gpio_dt_spec *dt)
{
	int rv;

	rv = gpio_emul_input_set_dt(dt, 0);
	if (rv)
		return rv;

	rv = gpio_emul_input_set_dt(dt, 1);
	if (rv)
		return rv;

	return 0;
}

static int set_usb_fault_alert_inputs(int hub, int a0, int a1)
{
	const struct gpio_dt_spec *gpio_usb_hub_fault_q_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_hub_fault_q_odl);
	const struct gpio_dt_spec *ioex_usb_a0_fault_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_a0_fault_odl);
	const struct gpio_dt_spec *ioex_usb_a1_fault_db_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_a1_fault_db_odl);
	int rv;

	rv = gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, hub);
	if (rv)
		return rv;

	rv = gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, a0);
	if (rv)
		return rv;

	rv = gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, a1);
	if (rv)
		return rv;

	return 0;
}

static int validate_usb_fault_alert_output(int hub, int a0, int a1)
{
	int rv;
	const struct gpio_dt_spec *gpio_usb_fault_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_fault_odl);

	rv = gpio_emul_output_get_dt(gpio_usb_fault_odl);
	return !(rv == (hub && a0 && a1));
}

/* Test suite and reset functions. */
static void test_reset(void)
{
	RESET_FAKE(pd_handle_overcurrent);
	RESET_FAKE(usb_charger_task_set_event);
	RESET_FAKE(battery_sleep_fuel_gauge);
	RESET_FAKE(charge_manager_get_active_charge_port);
	RESET_FAKE(pd_request_source_voltage);
	RESET_FAKE(charger_get_vbus_voltage);
	RESET_FAKE(usb_charge_set_mode);
	RESET_FAKE(pd_set_error_recovery);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(pd_is_battery_capable);

	memset(nct38xx_reset_toggles, 0, sizeof(nct38xx_reset_toggles));
	memset(ppc_vbus_sink_enable_enabled, 0,
	       sizeof(ppc_vbus_sink_enable_enabled));
	memset(pd_set_error_recovery_call_count, 0,
	       sizeof(pd_set_error_recovery_call_count));

	pd_set_error_recovery_fake.custom_fake = pd_set_error_recovery_mock;
	ppc_vbus_sink_enable_fake.custom_fake = ppc_vbus_sink_enable_mock;

	nct38xx_reset_notify(0);
	nct38xx_reset_notify(1);
}

/*
 * Certain tests change IOEX pin configurations to verify that they get
 * restored. The GPIO emulator doesn't reset pins to their original config
 * between tests. So we save and restore defaults manually.
 */
static gpio_flags_t ioex_c0_port0_saved[IOEX_GPIO_COUNT];
static gpio_flags_t ioex_c0_port1_saved[IOEX_GPIO_COUNT];
static gpio_flags_t ioex_c1_port0_saved[IOEX_GPIO_COUNT];
static gpio_flags_t ioex_c1_port1_saved[IOEX_GPIO_COUNT];

static void usbc_config_before(void *fixture)
{
	const struct device *ioex_c0_port0 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
	const struct device *ioex_c0_port1 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
	const struct device *ioex_c1_port0 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
	const struct device *ioex_c1_port1 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));

	ARG_UNUSED(fixture);
	test_reset();

	gpio_save_port_config(ioex_c0_port0, ioex_c0_port0_saved,
			      ARRAY_SIZE(ioex_c0_port0_saved));
	gpio_save_port_config(ioex_c0_port1, ioex_c0_port1_saved,
			      ARRAY_SIZE(ioex_c0_port1_saved));
	gpio_save_port_config(ioex_c1_port0, ioex_c1_port0_saved,
			      ARRAY_SIZE(ioex_c1_port0_saved));
	gpio_save_port_config(ioex_c1_port1, ioex_c1_port1_saved,
			      ARRAY_SIZE(ioex_c1_port1_saved));
}

static void usbc_config_after(void *fixture)
{
	const struct device *ioex_c0_port0 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
	const struct device *ioex_c0_port1 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
	const struct device *ioex_c1_port0 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
	const struct device *ioex_c1_port1 =
		DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));

	ARG_UNUSED(fixture);

	gpio_restore_port_config(ioex_c0_port0, ioex_c0_port0_saved,
				 ARRAY_SIZE(ioex_c0_port0_saved));
	gpio_restore_port_config(ioex_c0_port1, ioex_c0_port1_saved,
				 ARRAY_SIZE(ioex_c0_port1_saved));
	gpio_restore_port_config(ioex_c1_port0, ioex_c1_port0_saved,
				 ARRAY_SIZE(ioex_c1_port0_saved));
	gpio_restore_port_config(ioex_c1_port1, ioex_c1_port1_saved,
				 ARRAY_SIZE(ioex_c1_port1_saved));
}

ZTEST_SUITE(usbc_config, NULL, NULL, usbc_config_before, usbc_config_after,
	    NULL);

/* Test that our interrupts are being enabled. */
ZTEST(usbc_config, test_usbc_interrupt_init)
{
	const struct gpio_dt_spec *ioex_usb_c0_sbu_fault_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_fault_odl);
	const struct gpio_dt_spec *ioex_usb_c1_sbu_fault_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_fault_odl);
	const struct gpio_dt_spec *gpio_usb_c0_bc12_int_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_bc12_int_odl);
	const struct gpio_dt_spec *gpio_usb_c1_bc12_int_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_bc12_int_odl);

	/* Ensure interrupts are disabled. */
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12));
	gpio_disable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_usb_c0_sbu_fault));
	gpio_disable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_usb_c1_sbu_fault));

	/* usbc_interrupt_init should be called on init. */
	hook_notify(HOOK_INIT);

	/* Verify bc12 interrupt handler is called. */
	zassert_ok(toggle_pin_falling(gpio_usb_c0_bc12_int_odl));
	zassert_equal(usb_charger_task_set_event_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_fake.arg0_val, 0);
	zassert_equal(usb_charger_task_set_event_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
	RESET_FAKE(usb_charger_task_set_event);

	zassert_ok(toggle_pin_falling(gpio_usb_c1_bc12_int_odl));
	zassert_equal(usb_charger_task_set_event_fake.call_count, 1);
	zassert_equal(usb_charger_task_set_event_fake.arg0_val, 1);
	zassert_equal(usb_charger_task_set_event_fake.arg1_val,
		      USB_CHG_EVENT_BC12);
	RESET_FAKE(usb_charger_task_set_event);

	zassert_ok(toggle_pin_rising(gpio_usb_c0_bc12_int_odl));
	zassert_equal(usb_charger_task_set_event_fake.call_count, 0);
	RESET_FAKE(usb_charger_task_set_event);

	zassert_ok(toggle_pin_rising(gpio_usb_c1_bc12_int_odl));
	zassert_equal(usb_charger_task_set_event_fake.call_count, 0);
	RESET_FAKE(usb_charger_task_set_event);

	/*
	 * Verify that the fault handler calls pd_handle_overcurrent with the
	 * right port.
	 */
	zassert_ok(toggle_pin_falling(ioex_usb_c0_sbu_fault_odl));
	zassert_equal(pd_handle_overcurrent_fake.call_count, 1);
	zassert_equal(pd_handle_overcurrent_fake.arg0_val, 0);
	RESET_FAKE(pd_handle_overcurrent);

	zassert_ok(toggle_pin_falling(ioex_usb_c1_sbu_fault_odl));
	zassert_equal(pd_handle_overcurrent_fake.call_count, 1);
	zassert_equal(pd_handle_overcurrent_fake.arg0_val, 1);
	RESET_FAKE(pd_handle_overcurrent);
}

/* Test our fault interrupts. */
ZTEST(usbc_config, test_usb_fault_interrupt_init)
{
	const struct gpio_dt_spec *gpio_usb_hub_fault_q_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_hub_fault_q_odl);
	const struct gpio_dt_spec *ioex_usb_a0_fault_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_a0_fault_odl);
	const struct gpio_dt_spec *ioex_usb_a1_fault_db_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_a1_fault_db_odl);

	/* Make sure interrupts are disabled. */
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_hub_fault));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a0_fault));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a1_fault));

	/* usb_fault_interrupt_init should be called on chipset startup. */
	hook_notify(HOOK_CHIPSET_STARTUP);

	/* Validate that int_usb_hub_fault calls usb_fault_alert. */
	zassert_ok(set_usb_fault_alert_inputs(0, 0, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 0, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 0, 0));

	zassert_ok(set_usb_fault_alert_inputs(0, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 0, 1));

	zassert_ok(set_usb_fault_alert_inputs(0, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 1, 0));

	zassert_ok(set_usb_fault_alert_inputs(0, 1, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 1, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 1, 1));

	/* Validate that int_usb_hub_fault calls usb_fault_alert. */
	zassert_ok(set_usb_fault_alert_inputs(0, 0, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 0, 0));

	zassert_ok(set_usb_fault_alert_inputs(0, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 1, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 0, 1));

	zassert_ok(set_usb_fault_alert_inputs(1, 0, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 0, 0));

	zassert_ok(set_usb_fault_alert_inputs(1, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 1, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 0, 1));

	/* Validate that int_usb_hub_fault calls usb_fault_alert. */
	zassert_ok(set_usb_fault_alert_inputs(0, 0, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 0, 0));

	zassert_ok(set_usb_fault_alert_inputs(0, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(0, 1, 0));

	zassert_ok(set_usb_fault_alert_inputs(1, 0, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 0, 0));

	zassert_ok(set_usb_fault_alert_inputs(1, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 1));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 1, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 0));
	k_msleep(100);
	zassert_ok(validate_usb_fault_alert_output(1, 1, 0));
}

/* Test disabling fault interrupts. */
ZTEST(usbc_config, test_usb_fault_interrupt_disable)
{
	const struct gpio_dt_spec *gpio_usb_fault_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_fault_odl);
	const struct gpio_dt_spec *gpio_usb_hub_fault_q_odl =
		GPIO_DT_FROM_NODELABEL(gpio_usb_hub_fault_q_odl);
	const struct gpio_dt_spec *ioex_usb_a0_fault_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_a0_fault_odl);
	const struct gpio_dt_spec *ioex_usb_a1_fault_db_odl =
		GPIO_DT_FROM_NODELABEL(ioex_usb_a1_fault_db_odl);

	/* Make sure interrupts are enabled. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_hub_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a0_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a1_fault));

	/* usb_fault_interrupt_disable should be called on chipset startup. */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	zassert_ok(set_usb_fault_alert_inputs(0, 1, 1));
	zassert_ok(gpio_emul_input_set_dt(gpio_usb_hub_fault_q_odl, 1));
	k_msleep(100);
	zassert_false(gpio_emul_output_get_dt(gpio_usb_fault_odl));

	zassert_ok(set_usb_fault_alert_inputs(1, 0, 1));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a0_fault_odl, 1));
	k_msleep(100);
	zassert_false(gpio_emul_output_get_dt(gpio_usb_fault_odl));

	zassert_ok(set_usb_fault_alert_inputs(1, 1, 0));
	zassert_ok(gpio_emul_input_set_dt(ioex_usb_a1_fault_db_odl, 1));
	k_msleep(100);
	zassert_false(gpio_emul_output_get_dt(gpio_usb_fault_odl));
}

/* Test board_is_vbus_too_low function. */
ZTEST(usbc_config, test_board_is_vbus_too_low)
{
	/* Doesn't actually matter, underlying code doesn't use this value. */
	enum chg_ramp_vbus_state ramp_state = CHG_RAMP_VBUS_STABLE;

	charger_get_vbus_voltage_fake.return_val = 1;
	zassert_false(board_is_vbus_too_low(0, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 0);
	zassert_false(board_is_vbus_too_low(1, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 1);

	charger_get_vbus_voltage_fake.custom_fake =
		charger_get_vbus_voltage_mock;
	mock_voltage = 0;
	zassert_false(board_is_vbus_too_low(0, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 0);
	zassert_false(board_is_vbus_too_low(1, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 1);

	mock_voltage = SKYRIM_BC12_MIN_VOLTAGE / 2;
	zassert_true(board_is_vbus_too_low(0, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 0);
	zassert_true(board_is_vbus_too_low(1, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 1);

	mock_voltage = SKYRIM_BC12_MIN_VOLTAGE;
	zassert_false(board_is_vbus_too_low(0, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 0);
	zassert_false(board_is_vbus_too_low(1, ramp_state));
	zassert_equal(charger_get_vbus_voltage_fake.arg0_val, 1);
}

/* Test board hibernate functionality. */
ZTEST(usbc_config, test_board_hibernate)
{
	charge_manager_get_active_charge_port_fake.return_val =
		CHARGE_PORT_NONE;
	board_hibernate();
	zassert_equal(battery_sleep_fuel_gauge_fake.call_count, 1);
	RESET_FAKE(battery_sleep_fuel_gauge);
	RESET_FAKE(pd_request_source_voltage);

	charge_manager_get_active_charge_port_fake.return_val = 0;
	board_hibernate();
	zassert_equal(battery_sleep_fuel_gauge_fake.call_count, 1);
	zassert_equal(pd_request_source_voltage_fake.arg0_val, 0);
	zassert_equal(pd_request_source_voltage_fake.arg1_val,
		      SKYRIM_SAFE_RESET_VBUS_MV);
	RESET_FAKE(battery_sleep_fuel_gauge);
	RESET_FAKE(pd_request_source_voltage);

	charge_manager_get_active_charge_port_fake.return_val = 1;
	board_hibernate();
	zassert_equal(battery_sleep_fuel_gauge_fake.call_count, 1);
	zassert_equal(pd_request_source_voltage_fake.arg0_val, 1);
	zassert_equal(pd_request_source_voltage_fake.arg1_val,
		      SKYRIM_SAFE_RESET_VBUS_MV);
	RESET_FAKE(battery_sleep_fuel_gauge);
	RESET_FAKE(pd_request_source_voltage);
}

/*
 * The following section tests reset_nct38xx_port. This function should toggle
 * the reset pin to the nct38xx, save and restore the IO expanding GPIOs.
 */
/*
 * Helper function for testing reset_nct38xx_port. Returns how long the function
 * took to execute.
 */

static uint64_t run_reset_nct38xx(int port)
{
	timestamp_t t;
	uint64_t us;

	/* Ensure our test interrupts are enabled. */
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	/* Test C0. */
	t = get_time();
	reset_nct38xx_port(port);
	us = t.val - get_time().val;

	return us;
}

/* Test reset_nct38xx_port with an invalid port. */
ZTEST(usbc_config, test_reset_nct38xx_port_invalid)
{
	run_reset_nct38xx(3);

	zassert_equal(nct38xx_reset_toggles[0].call_count, 0);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 0);
}

/* Test reset_nct38xx_port on C0. */
ZTEST(usbc_config, test_reset_nct38xx_port_c0)
{
	uint64_t us = run_reset_nct38xx(0);

	zassert_equal(nct38xx_reset_toggles[0].call_count, 2);
	zassert_true(nct38xx_reset_toggles[0].us >=
		     NCT38XX_RESET_HOLD_DELAY_MS * 1000);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 0);
	zassert_true(us >= (NCT38XX_RESET_HOLD_DELAY_MS +
			    NCT3807_RESET_POST_DELAY_MS) *
				   1000);
}

/* Test reset_nct38xx_port on C1. */
ZTEST(usbc_config, test_reset_nct38xx_port_c1)
{
	uint64_t us = run_reset_nct38xx(1);

	zassert_equal(nct38xx_reset_toggles[0].call_count, 0);
	zassert_true(nct38xx_reset_toggles[1].us >=
		     NCT38XX_RESET_HOLD_DELAY_MS * 1000);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 2);
	zassert_true(us >= (NCT38XX_RESET_HOLD_DELAY_MS +
			    NCT3807_RESET_POST_DELAY_MS) *
				   1000);
}

/*
 * The following section tests that IO extender GPIOs are restored properly
 * during a reset. Tests cover pins configured to input, output low, and output
 * high.
 */

/* Helper func to check that GPIOs have been restored after port reset. */
static bool validate_nct38xx_reset_gpios(const gpio_flags_t *saved,
					 const gpio_flags_t *restored,
					 const int len)
{
	if (len != IOEX_GPIO_COUNT)
		return false;

	for (int i = 0; i < len; i++) {
		if (saved[i] != restored[i])
			return false;
	}

	return true;
}

/* Test reset_nct38xx_port restores C0 GPIOs configured as inputs. */
ZTEST(usbc_config, test_reset_nct38xx_port_c0_input)
{
	const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
	const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));

	gpio_flags_t template[IOEX_GPIO_COUNT] = { 0 };
	gpio_flags_t restored[IOEX_GPIO_COUNT] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(template); i++) {
		template[i] = GPIO_INPUT;
	}

	/* Configure the GPIO ports. */
	gpio_restore_port_config(port0, template, ARRAY_SIZE(template));
	gpio_restore_port_config(port1, template, ARRAY_SIZE(template));

	/* Reset C0. */
	reset_nct38xx_port(0);

	/* Verify that all ports have been restored correctly. */
	gpio_save_port_config(port0, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(template, restored,
						  ARRAY_SIZE(restored)));

	memset(restored, 0, sizeof(restored));
	gpio_save_port_config(port1, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(template, restored,
						  ARRAY_SIZE(restored)));
}

/* Test reset_nct38xx_port restores C1 GPIOs configured as inputs. */
ZTEST(usbc_config, test_reset_nct38xx_port_c1_input)
{
	const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
	const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));

	gpio_flags_t template[IOEX_GPIO_COUNT] = { 0 };
	gpio_flags_t restored[IOEX_GPIO_COUNT] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(template); i++) {
		template[i] = GPIO_INPUT;
	}

	/* Configure the GPIO ports. */
	gpio_restore_port_config(port0, template, ARRAY_SIZE(template));
	gpio_restore_port_config(port1, template, ARRAY_SIZE(template));

	/* Reset C1. */
	reset_nct38xx_port(1);

	/* Verify that all ports have been restored correctly. */
	gpio_save_port_config(port0, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));

	memset(restored, 0, sizeof(restored));
	gpio_save_port_config(port1, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));
}

/* Test reset_nct38xx_port restores C0 GPIOs when configured as high outputs. */
ZTEST(usbc_config, test_reset_nct38xx_port_c0_output_high)
{
	const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
	const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));

	gpio_flags_t template[IOEX_GPIO_COUNT] = { 0 };
	gpio_flags_t restored[IOEX_GPIO_COUNT] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(template); i++) {
		template[i] = GPIO_OUTPUT_HIGH;
	}

	/* Configure the GPIO ports. */
	gpio_restore_port_config(port0, template, ARRAY_SIZE(template));
	gpio_restore_port_config(port1, template, ARRAY_SIZE(template));

	/* Ensure our test interrupts are enabled. */
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	/* Reset C0. */
	reset_nct38xx_port(0);

	/* Verify that all ports have been restored correctly. */
	gpio_save_port_config(port0, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));

	memset(restored, 0, sizeof(restored));
	gpio_save_port_config(port1, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));
}

/* Test reset_nct38xx_port restores C1 GPIOs when configured as high outputs. */
ZTEST(usbc_config, test_reset_nct38xx_port_c1_output_high)
{
	const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
	const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));

	gpio_flags_t template[IOEX_GPIO_COUNT] = { 0 };
	gpio_flags_t restored[IOEX_GPIO_COUNT] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(template); i++) {
		template[i] = GPIO_OUTPUT_HIGH;
	}

	/* Configure the GPIO ports. */
	gpio_restore_port_config(port0, template, ARRAY_SIZE(template));
	gpio_restore_port_config(port1, template, ARRAY_SIZE(template));

	/* Ensure our test interrupts are enabled. */
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	/* Reset C0. */
	reset_nct38xx_port(1);

	/* Verify that all ports have been restored correctly. */
	gpio_save_port_config(port0, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));

	memset(restored, 0, sizeof(restored));
	gpio_save_port_config(port1, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));
}

/* Test reset_nct38xx_port restores C0 GPIOs when configured as low outputs. */
ZTEST(usbc_config, test_reset_nct38xx_port_c0_output_low)
{
	const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
	const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));

	gpio_flags_t template[IOEX_GPIO_COUNT] = { 0 };
	gpio_flags_t restored[IOEX_GPIO_COUNT] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(template); i++) {
		template[i] = GPIO_OUTPUT_LOW;
	}

	/* Configure the GPIO ports. */
	gpio_restore_port_config(port0, template, ARRAY_SIZE(template));
	gpio_restore_port_config(port1, template, ARRAY_SIZE(template));

	/* Ensure our test interrupts are enabled. */
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	/* Reset C0. */
	reset_nct38xx_port(0);

	/* Verify that all ports have been restored correctly. */
	gpio_save_port_config(port0, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));

	memset(restored, 0, sizeof(restored));
	gpio_save_port_config(port1, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));
}

/* Test reset_nct38xx_port restores C1 GPIOs when configured as low outputs. */
ZTEST(usbc_config, test_reset_nct38xx_port_c1_output_low)
{
	const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
	const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));

	gpio_flags_t template[IOEX_GPIO_COUNT] = { 0 };
	gpio_flags_t restored[IOEX_GPIO_COUNT] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(template); i++) {
		template[i] = GPIO_OUTPUT_LOW;
	}

	/* Configure the GPIO ports. */
	gpio_restore_port_config(port0, template, ARRAY_SIZE(template));
	gpio_restore_port_config(port1, template, ARRAY_SIZE(template));

	/* Ensure our test interrupts are enabled. */
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	/* Reset C0. */
	reset_nct38xx_port(1);

	/* Verify that all ports have been restored correctly. */
	gpio_save_port_config(port0, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));

	memset(restored, 0, sizeof(restored));
	gpio_save_port_config(port1, restored, ARRAY_SIZE(restored));
	zassert_true(validate_nct38xx_reset_gpios(restored, template,
						  ARRAY_SIZE(restored)));
}

/*
 * The following section tests combinations of dead battery and active charge
 * ports. With no charge port any dead battery ports should be reset.
 * If we have an actual charge port and an attached battery then any dead
 * battery ports should be reset.
 * If we don't have a battery then don't reset the active port since it'll cause
 * a brown-out.
 * The tests use calls to reset_nct38xx_port and pd_set_error_recovery to
 * validate behavior.
 */
static int config_port_dead_battery(int charge_port, int port0_mode,
				    int port1_mode)
{
	int rv;

	/* Enable our test resets to verify the nct38xx's reset line is toggled.
	 */
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	rv = tcpc_write(0, TCPC_REG_ROLE_CTRL, port0_mode);
	if (rv)
		return rv;
	rv = nct38xx_tcpm_init(0);
	if (rv)
		return rv;

	rv = tcpc_write(1, TCPC_REG_ROLE_CTRL, port1_mode);
	if (rv)
		return rv;
	rv = nct38xx_tcpm_init(1);
	if (rv)
		return rv;

	return board_set_active_charge_port(charge_port);
}

/* Test calling board_set_active_charge_port(CHARGE_PORT_NONE). */
ZTEST(usbc_config, test_board_set_active_charge_port_none)
{
	ppc_vbus_sink_enable_enabled[0] = true;
	ppc_vbus_sink_enable_enabled[1] = true;

	zassert_ok(config_port_dead_battery(CHARGE_PORT_NONE,
					    NCT38XX_ROLE_CTRL_DEAD_BATTERY,
					    NCT38XX_ROLE_CTRL_DEAD_BATTERY));

	/*
	 * Did a dead battery boot, both TCPCs should reset and
	 * pd_set_error_recovery called.
	 */
	zassert_equal(nct38xx_reset_toggles[0].call_count, 2);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 2);

	zassert_equal(pd_set_error_recovery_call_count[0], 1);
	zassert_equal(pd_set_error_recovery_call_count[1], 1);

	/* Check that vbus sink is disabled on both ports. */
	zassert_false(ppc_vbus_sink_enable_enabled[0]);
	zassert_false(ppc_vbus_sink_enable_enabled[1]);
}

/* Test board_set_active_charge_port argument validation. */
ZTEST(usbc_config, test_board_set_active_charge_port_invalid)
{
	zassert_true(board_set_active_charge_port(3) != 0);
}

/* Test dead battery on C0 and switching to C1 as charge port. */
ZTEST(usbc_config, test_board_set_active_charge_port_c1_c0_dead)
{
	pd_is_battery_capable_fake.return_val = true;

	zassert_ok(config_port_dead_battery(1, NCT38XX_ROLE_CTRL_DEAD_BATTERY,
					    NCT38XX_ROLE_CTRL_GOOD_BATTERY));

	zassert_equal(nct38xx_reset_toggles[0].call_count, 2);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 0);

	zassert_equal(pd_set_error_recovery_call_count[0], 1);
	zassert_equal(pd_set_error_recovery_call_count[1], 0);
}

/* Test dead battery on C0 and switching to C0 as charge port. */
ZTEST(usbc_config, test_board_set_active_charge_port_c0_c0_dead)
{
	pd_is_battery_capable_fake.return_val = true;

	zassert_true(config_port_dead_battery(0, NCT38XX_ROLE_CTRL_DEAD_BATTERY,
					      NCT38XX_ROLE_CTRL_GOOD_BATTERY));

	zassert_equal(nct38xx_reset_toggles[0].call_count, 2);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 0);

	zassert_equal(pd_set_error_recovery_call_count[0], 1);
	zassert_equal(pd_set_error_recovery_call_count[1], 0);
}

/* Test dead battery on C1 and switching to C1 as charge port. */
ZTEST(usbc_config, test_board_set_active_charge_port_c1_c1_dead)
{
	pd_is_battery_capable_fake.return_val = true;

	zassert_true(config_port_dead_battery(1, NCT38XX_ROLE_CTRL_GOOD_BATTERY,
					      NCT38XX_ROLE_CTRL_DEAD_BATTERY));

	zassert_equal(nct38xx_reset_toggles[0].call_count, 0);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 2);

	zassert_equal(pd_set_error_recovery_call_count[0], 0);
	zassert_equal(pd_set_error_recovery_call_count[1], 1);
}

/* Test dead battery on C0 and switching to C1 as charge port with no battery.
 */
ZTEST(usbc_config, test_board_set_active_charge_port_c1_c0_dead_no_battery)
{
	pd_is_battery_capable_fake.return_val = false;

	zassert_true(config_port_dead_battery(1, NCT38XX_ROLE_CTRL_DEAD_BATTERY,
					      NCT38XX_ROLE_CTRL_GOOD_BATTERY));
}

/* Test dead battery on C1 and switching to C0 as charge port with no battery */
ZTEST(usbc_config, test_board_set_active_charge_port_c0_c1_dead_no_battery)
{
	pd_is_battery_capable_fake.return_val = false;

	zassert_true(config_port_dead_battery(0, NCT38XX_ROLE_CTRL_GOOD_BATTERY,
					      NCT38XX_ROLE_CTRL_DEAD_BATTERY));
}

/*
 * Test dead battery on C0,C1 and switching to C1 as charge port with no
 * battery.
 */
ZTEST(usbc_config, test_board_set_active_charge_port_c1_c0_c1_dead_no_battery)
{
	pd_is_battery_capable_fake.return_val = false;

	zassert_ok(config_port_dead_battery(1, NCT38XX_ROLE_CTRL_DEAD_BATTERY,
					    NCT38XX_ROLE_CTRL_DEAD_BATTERY));

	zassert_equal(nct38xx_reset_toggles[0].call_count, 2);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 0);

	zassert_equal(pd_set_error_recovery_call_count[0], 1);
	zassert_equal(pd_set_error_recovery_call_count[1], 0);
}

/* Test dead battery on C1 and switching to C0 as charge port. */
ZTEST(usbc_config, test_board_set_active_charge_port_c0_c1_dead)
{
	pd_is_battery_capable_fake.return_val = true;

	zassert_ok(config_port_dead_battery(0, NCT38XX_ROLE_CTRL_GOOD_BATTERY,
					    NCT38XX_ROLE_CTRL_DEAD_BATTERY));

	zassert_equal(nct38xx_reset_toggles[0].call_count, 0);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 2);

	zassert_equal(pd_set_error_recovery_call_count[0], 0);
	zassert_equal(pd_set_error_recovery_call_count[1], 1);
}

/* Test dead battery on C0,C1 and switching to C0 as charge port with no battery
 */
ZTEST(usbc_config, test_board_set_active_charge_port_c0_c0_c1_dead_no_battery)
{
	pd_is_battery_capable_fake.return_val = false;

	zassert_ok(config_port_dead_battery(0, NCT38XX_ROLE_CTRL_DEAD_BATTERY,
					    NCT38XX_ROLE_CTRL_DEAD_BATTERY));

	zassert_equal(nct38xx_reset_toggles[0].call_count, 0);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 2);

	zassert_equal(pd_set_error_recovery_call_count[0], 0);
	zassert_equal(pd_set_error_recovery_call_count[1], 1);
}

/* Validate that board_reset_pd_mcu resets both ports. */
ZTEST(usbc_config, test_board_reset_pd_mcu)
{
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx0_rst));
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_test_nct38xx1_rst));

	board_reset_pd_mcu();

	/* Verify that both ports were reset through the reset lines. */
	zassert_equal(nct38xx_reset_toggles[0].call_count, 2);
	zassert_equal(nct38xx_reset_toggles[1].call_count, 2);
}
