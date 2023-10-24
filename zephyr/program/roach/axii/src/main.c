/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "console.h"
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "hooks.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define BASE_DETECT_INTERVAL (200 * MSEC)
#define ATTACH_MAX_THRESHOLD_MV 300
#define DETACH_MIN_THRESHOLD_MV 3000

const static struct device *one_wire_uart =
	DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static bool base_attached;
static uint8_t cached_kb_state[KEYBOARD_COLS_MAX];

static void base_update(bool attached)
{
	const struct gpio_dt_spec *ec_uart_pu_tester =
		GPIO_DT_FROM_NODELABEL(ec_uart_pu_tester);

	base_attached = attached;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp3300_base), attached);
	gpio_pin_configure(ec_uart_pu_tester->port, ec_uart_pu_tester->pin,
			   attached ? GPIO_OUTPUT_HIGH : GPIO_INPUT);

	if (attached) {
		/* call one_wire_uart_enable to reset its internal state */
		one_wire_uart_enable(one_wire_uart);
		memset(cached_kb_state, 0, KEYBOARD_COLS_MAX);
	}
}

static void base_detect_tick(void);
DECLARE_DEFERRED(base_detect_tick);

static void base_detect_tick(void)
{
	static bool debouncing;
	int mv = adc_read_channel(ADC_BASE_DET);

	if (mv >= DETACH_MIN_THRESHOLD_MV && base_attached) {
		if (!debouncing) {
			debouncing = true;
		} else {
			debouncing = false;
			base_update(false);
		}
	} else if (mv <= ATTACH_MAX_THRESHOLD_MV && !base_attached) {
		if (!debouncing) {
			debouncing = true;
		} else {
			debouncing = false;
			base_update(true);
		}
	} else {
		debouncing = false;
	}
	hook_call_deferred(&base_detect_tick_data, BASE_DETECT_INTERVAL);
}

test_export_static void recv_cb(uint8_t cmd, const uint8_t *payload, int length)
{
	if (cmd == ROACH_CMD_KEYBOARD_MATRIX && length == KEYBOARD_COLS_MAX) {
		/* convert key matrix to key event by comparing the payload to
		 * a cached previous state.
		 */
		for (int col = 0; col < KEYBOARD_COLS_MAX; col++) {
			if (cached_kb_state[col] == payload[col]) {
				continue;
			}

			uint8_t diff = cached_kb_state[col] ^ payload[col];

			while (diff) {
				int row = __builtin_ctz(diff);
				bool pressed = payload[col] & (1 << row);

				keyboard_state_changed(row, col, pressed);

				diff ^= (1 << row);
			}

			cached_kb_state[col] = payload[col];
		}
	}
	if (cmd == ROACH_CMD_TOUCHPAD_REPORT &&
	    length == sizeof(struct usb_hid_touchpad_report)) {
		set_touchpad_report((struct usb_hid_touchpad_report *)payload);
	}
}

static void axii_init(void)
{
	base_update(false);
	hook_call_deferred(&base_detect_tick_data, BASE_DETECT_INTERVAL);
	one_wire_uart_set_callback(one_wire_uart, recv_cb);
}
DECLARE_HOOK(HOOK_INIT, axii_init, HOOK_PRIO_DEFAULT);
