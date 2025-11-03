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

#include "keyboard_protocol.h"

static uint32_t fn_keys[] = DT_INST_PROP(0, keymap);

#define CODE_MASK 0xffff

static bool fn_key_pressed;
static uint32_t fn_keys_status;

static const uint32_t fn_key_rc = DT_INST_PROP(0, fn_rc);

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
		return;
	}

	int override_code = -1;
	if (!is_pressed) {
		/* Handle release regardless of Fn status */
		for (uint8_t i = 0; i < ARRAY_SIZE(fn_keys); i++) {
			if (!is_key(row, col, fn_keys[i])) {
				continue;
			}

			if ((fn_keys_status & BIT(i)) == 0) {
				continue;
			}

			override_code = fn_keys[i] & CODE_MASK;
			fn_keys_status &= ~BIT(i);

			break;
		}
	} else if (fn_key_pressed) {
		/* Handle press while holding Fn */
		for (uint8_t i = 0; i < ARRAY_SIZE(fn_keys); i++) {
			if (!is_key(row, col, fn_keys[i])) {
				continue;
			}

			override_code = fn_keys[i] & CODE_MASK;
			if (is_pressed) {
				fn_keys_status |= BIT(i);
			} else {
				fn_keys_status &= ~BIT(i);
			}

			break;
		}
	}

	LOG_DBG("fn_key_pressed=%d fn_keys_status=%02x", fn_key_pressed,
		fn_keys_status);

	keyboard_state_changed_process(row, col, is_pressed, override_code);
}
