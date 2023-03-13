/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "console.h"
#include "drivers/one_wire_uart.h"
#include "hid_over_i2c_target.h"
#include "hooks.h"
#include "keyboard_mkbp.h"
#include "roach_cmds.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

const static struct device *one_wire_uart =
	DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static void recv_cb(uint8_t cmd, const uint8_t *payload, int length)
{
	if (cmd == ROACH_CMD_KEYBOARD_MATRIX && length == KEYBOARD_COLS_MAX) {
		mkbp_keyboard_add(payload);
	}
	if (cmd == ROACH_CMD_TOUCHPAD_REPORT &&
	    length == sizeof(struct usb_hid_touchpad_report)) {
		hid_i2c_touchpad_add(
			(const struct usb_hid_touchpad_report *)payload);
	}
}

static void base_shutdown_hook(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_SHUTDOWN:
		one_wire_uart_send(one_wire_uart, ROACH_CMD_SUSPEND, NULL, 0);
		break;
	case AP_POWER_STARTUP:
		one_wire_uart_send(one_wire_uart, ROACH_CMD_RESUME, NULL, 0);
		break;
	default:
		return;
	}
}

static void ec_ec_comm_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, base_shutdown_hook,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	one_wire_uart_set_callback(one_wire_uart, recv_cb);
	one_wire_uart_enable(one_wire_uart);

	i2c_target_driver_register(DEVICE_DT_GET(DT_NODELABEL(hid_i2c_target)));

	/* b/300403990: put these here before driver ready */
	*(volatile uint8_t *)0xf03a23 = 1; /* UART1PMR */

	uint8_t lcr_cache = *(volatile uint8_t *)0xf02803;

	*(volatile uint8_t *)0xf02803 |= 0x80; /* access divisor latches */
	*(volatile uint8_t *)0xf02800 = 0x01; /* set divisor = 0x8001 */
	*(volatile uint8_t *)0xf02801 = 0x80;
	*(volatile uint8_t *)0xf02803 = lcr_cache;
	*(volatile uint8_t *)0xf02808 = 2; /* high speed select */
}
DECLARE_HOOK(HOOK_INIT, ec_ec_comm_init, HOOK_PRIO_DEFAULT);
