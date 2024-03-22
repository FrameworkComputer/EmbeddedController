/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_protocol.h"
#include "keyboard_scan.h"

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(kbd_input, CONFIG_INPUT_LOG_LEVEL);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

static atomic_t disable_scan_mask;

void keyboard_scan_enable(int enable, enum kb_scan_disable_masks mask)
{
	if (enable) {
		atomic_and(&disable_scan_mask, ~mask);
	} else {
		atomic_or(&disable_scan_mask, mask);
	}
}

static void keyboard_input_cb(struct input_event *evt)
{
	static int row;
	static int col;
	static bool pressed;

	switch (evt->code) {
	case INPUT_ABS_X:
		col = evt->value;
		break;
	case INPUT_ABS_Y:
		row = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		pressed = evt->value;
		break;
	}

	if (atomic_get(&disable_scan_mask) != 0) {
		return;
	}

	if (evt->sync) {
		LOG_DBG("keyboard_state_changed %d %d %d", row, col, pressed);
		keyboard_state_changed(row, col, pressed);
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE), keyboard_input_cb);
