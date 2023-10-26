/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "keyboard_config.h"
#include "test_state.h"
#include "usb_hid_touchpad.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(keyboard_state_changed, int, int, int);
FAKE_VOID_FUNC(set_touchpad_report, struct usb_hid_touchpad_report *);

static const struct device *adc0 = DEVICE_DT_GET(DT_NODELABEL(adc0));

void recv_cb(uint8_t cmd, const uint8_t *payload, int length);

ZTEST(axii, test_attach_detach)
{
	const struct gpio_dt_spec *en_pp3300_base =
		GPIO_DT_FROM_NODELABEL(en_pp3300_base);

	/* attach */
	adc_emul_const_value_set(adc0, 0, 100);
	k_msleep(1000);
	zassert_equal(gpio_emul_output_get(en_pp3300_base->port,
					   en_pp3300_base->pin),
		      1);

	/* detach */
	adc_emul_const_value_set(adc0, 0, 3300);
	k_msleep(1000);
	zassert_equal(gpio_emul_output_get(en_pp3300_base->port,
					   en_pp3300_base->pin),
		      0);
}

ZTEST(axii, test_keyboard_event)
{
	uint8_t key_matrix[KEYBOARD_COLS_MAX] = {};

	key_matrix[5] = (1 << 6);
	recv_cb(ROACH_CMD_KEYBOARD_MATRIX, key_matrix, sizeof(key_matrix));

	key_matrix[3] = (1 << 4);
	recv_cb(ROACH_CMD_KEYBOARD_MATRIX, key_matrix, sizeof(key_matrix));

	key_matrix[3] = 0;
	recv_cb(ROACH_CMD_KEYBOARD_MATRIX, key_matrix, sizeof(key_matrix));

	key_matrix[5] = 0;
	recv_cb(ROACH_CMD_KEYBOARD_MATRIX, key_matrix, sizeof(key_matrix));

	zassert_equal(keyboard_state_changed_fake.call_count, 4);

	/* 1st call: keyboard_state_changed(6, 5, 1) */
	zassert_equal(keyboard_state_changed_fake.arg0_history[0], 6);
	zassert_equal(keyboard_state_changed_fake.arg1_history[0], 5);
	zassert_equal(keyboard_state_changed_fake.arg2_history[0], 1);

	/* 2nd call: keyboard_state_changed(4. 3, 1) */
	zassert_equal(keyboard_state_changed_fake.arg0_history[1], 4);
	zassert_equal(keyboard_state_changed_fake.arg1_history[1], 3);
	zassert_equal(keyboard_state_changed_fake.arg2_history[1], 1);

	/* 3rd call: keyboard_state_changed(4, 3, 0) */
	zassert_equal(keyboard_state_changed_fake.arg0_history[2], 4);
	zassert_equal(keyboard_state_changed_fake.arg1_history[2], 3);
	zassert_equal(keyboard_state_changed_fake.arg2_history[2], 0);

	/* 4th call: keyboard_state_changed(6, 5, 0) */
	zassert_equal(keyboard_state_changed_fake.arg0_history[3], 6);
	zassert_equal(keyboard_state_changed_fake.arg1_history[3], 5);
	zassert_equal(keyboard_state_changed_fake.arg2_history[3], 0);
}

ZTEST(axii, test_touchpad_event)
{
	struct usb_hid_touchpad_report report;

	recv_cb(ROACH_CMD_TOUCHPAD_REPORT, (uint8_t *)&report, sizeof(report));
	zassert_equal(set_touchpad_report_fake.call_count, 1);
}

static void consume_uart_tx(const struct device *dev, size_t size,
			    void *user_data)
{
	uart_emul_flush_tx_data(dev);
}

static void axii_before(void *fixture)
{
	RESET_FAKE(set_touchpad_report);
	RESET_FAKE(keyboard_state_changed);

	/* set base detached by default */
	adc_emul_const_value_set(adc0, 0, 3300);

	uart_emul_callback_tx_data_ready_set(DEVICE_DT_GET(DT_NODELABEL(uart2)),
					     consume_uart_tx, NULL);
}

ZTEST_SUITE(axii, roach_predicate_post_main, NULL, axii_before, NULL, NULL);
