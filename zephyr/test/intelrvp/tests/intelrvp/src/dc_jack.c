/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "fakes.h"
#include "hooks.h"
#include "intelrvp.h"
#include "lpc.h"
#include "usb_charge.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <gpio/gpio_int.h>

__overridable const int supplier_priority[] = {
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	[CHARGE_SUPPLIER_DEDICATED] = 0,
#endif
	[CHARGE_SUPPLIER_PD] = 1,
	[CHARGE_SUPPLIER_TYPEC] = 2,
	[CHARGE_SUPPLIER_TYPEC_DTS] = 2,
#ifdef CONFIG_USB_CHARGER
	[CHARGE_SUPPLIER_PROPRIETARY] = 3,
	[CHARGE_SUPPLIER_BC12_DCP] = 3,
	[CHARGE_SUPPLIER_BC12_CDP] = 3,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_TYPEC_UNDER_1_5A] = 4,
	[CHARGE_SUPPLIER_OTHER] = 4,
	[CHARGE_SUPPLIER_VBUS] = 4,
#endif

};

FAKE_VOID_FUNC(charge_manager_update_charge, int, int,
	       const struct charge_port_info *);
FAKE_VALUE_FUNC(const struct batt_params *, charger_current_battery_params);
FAKE_VALUE_FUNC(int, charge_get_display_charge);

struct charge_port_info port_info = {
	.current = 0,
	.voltage = 0,
};

#define device_dt(gpio_name)                                              \
	{                                                                 \
		.port = DEVICE_DT_GET(                                    \
			DT_GPIO_CTLR(DT_NODELABEL(gpio_name), gpios)),    \
		.pin = DT_GPIO_PIN(DT_NODELABEL(gpio_name), gpios),       \
		.dt_flags = 0xFF &                                        \
			    DT_GPIO_FLAGS(DT_NODELABEL(gpio_name), gpios) \
	}

const struct gpio_dt_spec dc_jack_gpio_device = device_dt(std_adp_prsnt);

static void set_dc_jack_gpio()
{
	gpio_pin_configure(dc_jack_gpio_device.port, dc_jack_gpio_device.pin,
			   (GPIO_INPUT | GPIO_ACTIVE_HIGH));
	gpio_emul_input_set(dc_jack_gpio_device.port, dc_jack_gpio_device.pin,
			    1);
}

static void reset_dc_jack_gpio()
{
	gpio_pin_configure(dc_jack_gpio_device.port, dc_jack_gpio_device.pin,
			   (GPIO_INPUT | GPIO_ACTIVE_HIGH));
	gpio_emul_input_set(dc_jack_gpio_device.port, dc_jack_gpio_device.pin,
			    0);
}

ZTEST_USER(test_dc_jack, test_board_is_dc_jack_present)
{
	/* DC Jack gpio set */
	set_dc_jack_gpio();
	zassert_equal(1, board_is_dc_jack_present(), "value:%d",
		      board_is_dc_jack_present());

	/* DC Jack gpio reset */
	reset_dc_jack_gpio();
	zassert_equal(0, board_is_dc_jack_present());
}

static void
mock_charge_manager_update_charge(int port, int en,
				  const struct charge_port_info *info)
{
	port_info.current = info->current;
	port_info.voltage = info->voltage;
}

ZTEST_USER(test_dc_jack, test_charger_jack_init_present)
{
	charge_manager_update_charge_fake.custom_fake =
		mock_charge_manager_update_charge;

	set_dc_jack_gpio();
	/* Running the hook will check if dc jack present */
	board_charge_init();
	k_sleep(K_MSEC(500));

	/* Since dc jack gpio is set in test_board_is_dc_jack_present port
	 * current will be non zero */
	zassert_equal((CONFIG_PLATFORM_EC_PD_MAX_POWER_MW * 1000) /
			      DC_JACK_MAX_VOLTAGE_MV,
		      port_info.current, "port current:%d", port_info.current);
	zassert_equal(DC_JACK_MAX_VOLTAGE_MV, port_info.voltage,
		      "port voltage:%d", port_info.voltage);
}

ZTEST_USER(test_dc_jack, test_charger_jack_init_not_present)
{
	charge_manager_update_charge_fake.custom_fake =
		mock_charge_manager_update_charge;

	reset_dc_jack_gpio();
	/* Running the hook will check if dc jack present */
	board_charge_init();
	k_sleep(K_MSEC(500));

	/* Since dc jack gpio is reset in test_board_is_dc_jack_present port
	 * current will be zero */
	zassert_equal(0, port_info.current, "port current:%d",
		      port_info.current);
	zassert_equal(USB_CHARGER_VOLTAGE_MV, port_info.voltage,
		      "port voltage:%d", port_info.voltage);
}

ZTEST_USER(test_dc_jack, test_charger_jack_interrupt)
{
	charge_manager_update_charge_fake.custom_fake =
		mock_charge_manager_update_charge;

	reset_dc_jack_gpio();

	/* Delay to process interrupt */
	k_sleep(K_MSEC(500));

	/* Since dc jack gpio is reset in test_board_is_dc_jack_present port
	 * current will be zero */
	zassert_equal(0, port_info.current, "port current:%d",
		      port_info.current);
	zassert_equal(USB_CHARGER_VOLTAGE_MV, port_info.voltage,
		      "port voltage:%d", port_info.voltage);

	/* DC Jack gpio set */
	set_dc_jack_gpio();

	/* Delay to process interrupt */
	k_sleep(K_MSEC(500));

	zassert_equal((CONFIG_PLATFORM_EC_PD_MAX_POWER_MW * 1000) /
			      DC_JACK_MAX_VOLTAGE_MV,
		      port_info.current, "port current:%d", port_info.current);
	zassert_equal(DC_JACK_MAX_VOLTAGE_MV, port_info.voltage,
		      "port voltage:%d", port_info.voltage);
}

ZTEST_SUITE(test_dc_jack, NULL, NULL, NULL, NULL, NULL);
