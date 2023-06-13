/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "drivers/one_wire_uart.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "roach_cmds.h"
#include "touchpad.h"
#include "usb_hid_touchpad.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

const static struct device *one_wire_uart =
	DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static void recv_cb(uint8_t cmd, const uint8_t *payload, int length)
{
	/* TODO: handle ROACH_CMD_SUSPEND/RESUME after touchpad driver ready */
}

static void ec_ec_comm_init(void)
{
	one_wire_uart_set_callback(one_wire_uart, recv_cb);
	one_wire_uart_enable(DEVICE_DT_GET(DT_NODELABEL(one_wire_uart)));

	/* b/300403990: put these here before driver ready */
	uint8_t lcr_cache = *(volatile uint8_t *)0xf02803;

	*(volatile uint8_t *)0xf02803 |= 0x80; /* access divisor latches */
	*(volatile uint8_t *)0xf02800 = 0x01; /* set divisor = 0x8001 */
	*(volatile uint8_t *)0xf02801 = 0x80;
	*(volatile uint8_t *)0xf02803 = lcr_cache;
	*(volatile uint8_t *)0xf02808 = 2; /* high speed select */
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

void board_touchpad_reset(void)
{
}

void set_touchpad_report(struct usb_hid_touchpad_report *report)
{
	one_wire_uart_send(one_wire_uart, ROACH_CMD_TOUCHPAD_REPORT,
			   (uint8_t *)report, sizeof(*report));
}
