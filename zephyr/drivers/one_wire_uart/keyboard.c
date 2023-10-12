/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "touchpad.h"
#include "usb_hid_touchpad.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

const static struct device *one_wire_uart =
	DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static void ec_ec_comm_init(void)
{
	one_wire_uart_enable(DEVICE_DT_GET(DT_NODELABEL(one_wire_uart)));
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
