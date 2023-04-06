/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "battery.h"
#include "charger.h"
#include "hooks.h"
#include "ocpc.h"
#include "usb_pd.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
FAKE_VALUE_FUNC(int, board_set_active_charge_port, int);
FAKE_VALUE_FUNC(uint8_t, board_get_usb_pd_port_count);
FAKE_VALUE_FUNC(int, power_button_is_pressed);

static void suite_before(void *fixture)
{
	RESET_FAKE(battery_is_present);
	RESET_FAKE(board_get_usb_pd_port_count);
	RESET_FAKE(board_set_active_charge_port);
	RESET_FAKE(power_button_is_pressed);
}

ZTEST_SUITE(nissa_common, NULL, NULL, suite_before, NULL, NULL);

ZTEST(nissa_common, test_pen_power_control)
{
	const struct gpio_dt_spec *const pen_power =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_pen_x);

	hook_notify(HOOK_INIT);
	zassert_false(gpio_emul_output_get(pen_power->port, pen_power->pin),
		      "Pen power should be off by default");

	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_true(gpio_emul_output_get(pen_power->port, pen_power->pin),
		     "Pen power should be on after AP startup");

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_false(gpio_emul_output_get(pen_power->port, pen_power->pin),
		      "Pen power should be off after AP shutdown");
}

ZTEST(nissa_common, test_hibernate)
{
	const struct gpio_dt_spec *const hibernate_enable =
		GPIO_DT_FROM_NODELABEL(gpio_en_slp_z);

	zassert_false(gpio_emul_output_get(hibernate_enable->port,
					   hibernate_enable->pin),
		      "Hibernate pin should be low by default");
	board_hibernate_late();
	zassert_true(gpio_emul_output_get(hibernate_enable->port,
					  hibernate_enable->pin),
		     "Hibernate pin should go high to hibernate");
}

ZTEST(nissa_common, test_vconn_swap)
{
	const struct gpio_dt_spec *const dsw_pwrok =
		GPIO_DT_FROM_NODELABEL(gpio_ec_soc_dsw_pwrok);

	/* AP 5V rail is off. */
	zassert_false(gpio_pin_get_dt(dsw_pwrok));
	zassert_false(pd_check_vconn_swap(0));
	zassert_false(pd_check_vconn_swap(1));

	/*
	 * Case with the rail on is untestable because emulated GPIOs don't
	 * allow getting the current value of output pins.
	 */
}

ZTEST(nissa_common, test_ocpc_configuration)
{
	int kp, kp_div;
	int ki, ki_div;
	int kd, kd_div;
	struct ocpc_data ocpc_data = {};

	ocpc_get_pid_constants(&kp, &kp_div, &ki, &ki_div, &kd, &kd_div);

	/*
	 * Only proportional control is used, at 1/32 gain. Gain of integral and
	 * derivative terms is zero.
	 */
	zassert_equal(kp, 1);
	zassert_equal(kp_div, 32);
	zassert_equal(ki, 0);
	zassert_not_equal(ki_div, 0);
	zassert_equal(kd, 0);
	zassert_not_equal(kd_div, 0);

	/* With two chargers, we note that Isys can't be measured. */
	zassert_equal(CONFIG_USB_PD_PORT_MAX_COUNT, 2);
	board_get_usb_pd_port_count_fake.return_val = 2;
	board_ocpc_init(&ocpc_data);
	zassert_equal(ocpc_data.chg_flags[1], OCPC_NO_ISYS_MEAS_CAP);
}

ZTEST(nissa_common, test_sm5803_buck_boost_forbidden)
{
	zassert_true(pd_is_valid_input_voltage(12000));

	/*
	 * TODO(b:267959470): use SM5803 emulator for this test so behavior can
	 * be verified with assorted battery cell counts.
	 */
}
