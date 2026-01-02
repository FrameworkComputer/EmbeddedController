/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_8042_sharedlib.h"

#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#define DT_DRV_COMPAT cros_ec_scancode_set2

/* clang-format off */

#define FOREACH_COL(node_id, fn) \
	fn(node_id, 0) fn(node_id, 1) fn(node_id, 2) fn(node_id, 3) \
	fn(node_id, 4) fn(node_id, 5) fn(node_id, 6) fn(node_id, 7) \
	fn(node_id, 8) fn(node_id, 9) fn(node_id, 10) fn(node_id, 11) \
	fn(node_id, 12) fn(node_id, 13) fn(node_id, 14) fn(node_id, 15) \
	fn(node_id, 16) fn(node_id, 17)

#define DT_COL_CHECK(node_id, n) \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, col##n##_codes), ( \
	BUILD_ASSERT(n < KEYBOARD_COLS_MAX, "extra col codes: " STRINGIFY(n)); \
	BUILD_ASSERT(DT_PROP_LEN(node_id, col##n##_codes) == 8, \
		     STRINGIFY(col##n##_codes) " must have 8 entries"); \
	),( \
	BUILD_ASSERT(n >= KEYBOARD_COLS_MAX, "missing col codes: " STRINGIFY(n)); \
	))

#define DT_COL(node_id, n) \
	IF_ENABLED(DT_NODE_HAS_PROP(node_id, col##n##_codes), ( \
	DT_PROP(node_id, col##n##_codes), \
	))

static uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];

#define SET2_CHILD_COL_CHECK(node_id) FOREACH_COL(node_id, DT_COL_CHECK)

DT_INST_FOREACH_CHILD(0, SET2_CHILD_COL_CHECK)

#define SET2_CHILD_INIT(node_id) { FOREACH_COL(node_id, DT_COL) },

static const uint16_t extra_scancodes[][KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	DT_INST_FOREACH_CHILD(0, SET2_CHILD_INIT)
};

#define SET2_CONFIG_COUNT ARRAY_SIZE(extra_scancodes)

BUILD_ASSERT(SET2_CONFIG_COUNT > 0, "need to define at least one scancode set");

/* clang-format on */

test_mockable uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		return scancode_set2[col][row];
	}

	return 0;
}

test_mockable void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS) {
		scancode_set2[col][row] = val;
	}
}

void set_scancode_set2_table(uint8_t idx)
{
	if (idx >= SET2_CONFIG_COUNT) {
		return;
	}

	memcpy(scancode_set2, extra_scancodes[idx], sizeof(scancode_set2));
}

static int scancode_set2_load_default(void)
{
	set_scancode_set2_table(0);
	return 0;
}

SYS_INIT(scancode_set2_load_default, POST_KERNEL, 0);
