/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "consumer.h"
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "drivers/one_wire_uart_stream.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "queue.h"
#include "touchpad.h"
#include "usb_hid_touchpad.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_USB, format, ##args)

const static struct device *one_wire_uart =
	DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

void updater_stream_written(const struct consumer *consumer, size_t count)
{
	while (!queue_is_empty(consumer->queue)) {
		struct queue_chunk chunk =
			queue_get_read_chunk(consumer->queue);
		int ret;

		ret = one_wire_uart_send(one_wire_uart,
					 ROACH_CMD_UPDATER_COMMAND,
					 chunk.buffer, chunk.count);
		if (ret) {
			CPRINTS("%s: tx queue full", __func__);
		}
		queue_advance_head(consumer->queue, chunk.count);
	}
}

static void recv_cb(uint8_t cmd, const uint8_t *payload, int length)
{
	/* TODO(b/277667319): handle ROACH_CMD_SUSPEND/RESUME after touchpad
	 * driver ready.
	 */

	if (cmd == ROACH_CMD_UPDATER_COMMAND) {
		const struct queue *usb_to_update = usb_update.producer.queue;

		QUEUE_ADD_UNITS(usb_to_update, payload, length);
	}
}

static void ec_ec_comm_init(void)
{
	one_wire_uart_set_callback(one_wire_uart, recv_cb);
	one_wire_uart_enable(one_wire_uart);
}
DECLARE_HOOK(HOOK_INIT, ec_ec_comm_init, HOOK_PRIO_DEFAULT);

void keyboard_state_changed(int row, int col, int is_pressed)
{
	uint8_t state[KEYBOARD_COLS_MAX];

	memcpy(state, keyboard_scan_get_state(), KEYBOARD_COLS_MAX);
	if (is_pressed) {
		state[col] |= BIT(row);
	} else {
		state[col] &= ~BIT(row);
	}

	one_wire_uart_send(one_wire_uart, ROACH_CMD_KEYBOARD_MATRIX, state,
			   KEYBOARD_COLS_MAX);
}

void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	one_wire_uart_send(one_wire_uart, ROACH_CMD_TOUCHPAD_REPORT,
			   (uint8_t *)report, sizeof(*report));
}
