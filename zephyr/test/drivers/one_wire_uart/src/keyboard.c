/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "drivers/one_wire_uart_stream.h"
#include "keyboard_config.h"
#include "keyboard_scan.h"
#include "queue.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "touchpad.h"
#include "usb_hid_touchpad.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

void keyboard_state_changed(int row, int col, int is_pressed);

const static struct device *dev = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static const struct queue update_to_usb = QUEUE_NULL(64, uint8_t);
static const struct queue usb_to_update = QUEUE_NULL(64, uint8_t);
USB_STREAM_CONFIG_FULL(usb_update, 0, 0, 0, 0, 0, 0, 0, 0, usb_to_update,
		       update_to_usb, 0, 0);

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

ZTEST(one_wire_uart_keyboard, test_ap_to_updater)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	const uint8_t expected[4] = { 1, 2, 3, 4 };
	uint8_t actual[4];

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = sizeof(expected) + 1;
	msg.header.sender = 1;
	msg.header.msg_id = 1;
	msg.payload[0] = ROACH_CMD_UPDATER_COMMAND;
	memcpy(msg.payload + 1, expected, sizeof(expected));
	msg.header.checksum = checksum(&msg);

	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + msg.header.payload_len);

	process_rx_fifo(dev);
	process_packet();

	zassert_equal(queue_count(&usb_to_update), sizeof(expected));
	queue_remove_units(&usb_to_update, actual, sizeof(actual));
	zassert_mem_equal(actual, expected, sizeof(expected));
}

ZTEST(one_wire_uart_keyboard, test_updater_to_ap)
{
	struct one_wire_uart_data *data = dev->data;
	const uint8_t expected[4] = { 1, 2, 3, 4 };
	struct one_wire_uart_message msg;

	queue_add_units(&update_to_usb, expected, sizeof(expected));
	usb_update.consumer.ops->written(&usb_update.consumer,
					 sizeof(expected));

	zassert_true(queue_is_empty(&update_to_usb));
	zassert_equal(k_msgq_num_used_get(data->tx_queue), 1);
	k_msgq_get(data->tx_queue, &msg, K_NO_WAIT);
	zassert_equal(msg.payload[0], ROACH_CMD_UPDATER_COMMAND);
	zassert_mem_equal(msg.payload + 1, expected, sizeof(expected));
}

static void keyboard_before(void *fixture)
{
	one_wire_uart_reset(dev);

	RESET_FAKE(mkbp_keyboard_add);
}

ZTEST_SUITE(one_wire_uart_keyboard, drivers_predicate_post_main, NULL,
	    keyboard_before, NULL, NULL);
