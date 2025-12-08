/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 8042 keyboard Fn key handling
 */

#define DT_DRV_COMPAT cros_ec_fn_keys

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/input/input_keymap.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/kbd.h>

LOG_MODULE_REGISTER(fn_keys, LOG_LEVEL_INF);

#include "keyboard_config.h"
#include "keyboard_protocol.h"

static uint32_t fn_keys[] = DT_INST_PROP(0, keymap);

static bool fn_key_pressed;
static bool fn_key_triggered;
static uint32_t fn_keys_status;
static uint8_t normal_keys_status[KEYBOARD_COLS_MAX];

static const uint16_t fn_key_rc = DT_INST_PROP(0, fn_rc);

static bool is_key(int row, int col, uint32_t rc)
{
	if (row == MATRIX_ROW(rc) && col == MATRIX_COL(rc)) {
		return true;
	}

	return false;
}

void keyboard_state_changed(int row, int col, int is_pressed)
{
	if (KBD_RC(row, col) == fn_key_rc) {
		fn_key_pressed = is_pressed;

		if (is_pressed) {
			fn_key_triggered = false;
		} else if (!fn_key_triggered) {
			/* trigger a press and release of the Fn key if nothing
			 * else has been pressed
			 */
			keyboard_state_changed_process(row, col, true, -1);
			keyboard_state_changed_process(row, col, false, -1);
		}

		return;
	}

	if (!is_pressed) {
		/* Handle release regardless of Fn status */
		for (uint8_t i = 0; i < ARRAY_SIZE(fn_keys); i++) {
			int override_code;

			if (!is_key(row, col, fn_keys[i])) {
				continue;
			}

			if ((fn_keys_status & BIT(i)) == 0) {
				continue;
			}

			override_code = MATRIX_CODE(fn_keys[i]);
			fn_keys_status &= ~BIT(i);

			keyboard_state_changed_process(row, col, is_pressed,
						       override_code);

			return;
		}

		if ((normal_keys_status[col] & BIT(row)) == 0) {
			/* Discard, key press code was not sent */
			return;
		}
	} else if (fn_key_pressed) {
		/* Handle press while holding Fn */
		fn_key_triggered = true;

		for (uint8_t i = 0; i < ARRAY_SIZE(fn_keys); i++) {
			int override_code;

			if (!is_key(row, col, fn_keys[i])) {
				continue;
			}

			override_code = MATRIX_CODE(fn_keys[i]);
			fn_keys_status |= BIT(i);

			keyboard_state_changed_process(row, col, is_pressed,
						       override_code);

			return;
		}

		/* Do not emit a code if the key is not mapped */
		return;
	}

	if (is_pressed) {
		normal_keys_status[col] |= BIT(row);
	} else {
		normal_keys_status[col] &= ~BIT(row);
	}

	keyboard_state_changed_process(row, col, is_pressed, -1);
}
