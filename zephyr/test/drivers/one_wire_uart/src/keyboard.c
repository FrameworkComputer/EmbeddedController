/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "keyboard_config.h"
#include "touchpad.h"
#include "usb_hid_touchpad.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

const static struct device *dev = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

FAKE_VALUE_FUNC(int, mkbp_keyboard_add, const uint8_t *);

ZTEST(one_wire_uart_keyboard, test_keyboard_event)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	uint8_t expected_key_state[KEYBOARD_COLS_MAX] = {};

	test_keyboard_scan_debounce_reset();

	keyboard_state_changed(0, 0, 1);
	zassert_equal(k_msgq_num_used_get(data->tx_queue), 1);
	zassert_ok(k_msgq_get(data->tx_queue, &msg, K_NO_WAIT));
	zassert_equal(msg.header.payload_len, KEYBOARD_COLS_MAX + 1);
	zassert_equal(msg.payload[0], ROACH_CMD_KEYBOARD_MATRIX);
	expected_key_state[0] = 1;
	zassert_mem_equal(msg.payload + 1, expected_key_state,
			  KEYBOARD_COLS_MAX);

	keyboard_state_changed(0, 0, 0);
	zassert_ok(k_msgq_get(data->tx_queue, &msg, K_NO_WAIT));
	zassert_equal(msg.header.payload_len, KEYBOARD_COLS_MAX + 1);
	zassert_equal(msg.payload[0], ROACH_CMD_KEYBOARD_MATRIX);
	expected_key_state[0] = 0;
	zassert_mem_equal(msg.payload + 1, expected_key_state,
			  KEYBOARD_COLS_MAX);
}

ZTEST(one_wire_uart_keyboard, test_touchpad_event)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	struct usb_hid_touchpad_report report;

	set_touchpad_report(&report);
	zassert_ok(k_msgq_get(data->tx_queue, &msg, K_NO_WAIT));
	zassert_equal(msg.header.payload_len, sizeof(report) + 1);
	zassert_equal(msg.payload[0], ROACH_CMD_TOUCHPAD_REPORT);
	zassert_mem_equal(msg.payload + 1, &report, sizeof(report));
}

static void keyboard_before(void *fixture)
{
	one_wire_uart_reset(dev);

	RESET_FAKE(mkbp_keyboard_add);
	one_wire_uart_set_callback(dev, NULL);
}

ZTEST_SUITE(one_wire_uart_keyboard, NULL, NULL, keyboard_before, NULL, NULL);
