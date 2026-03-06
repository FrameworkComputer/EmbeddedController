/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_customization.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

struct keyboard_type key_typ = {
	.col_down = JAINA_KEYBOARD_COL_DOWN,
	.row_down = JAINA_KEYBOARD_ROW_DOWN,
	.col_esc = JAINA_KEYBOARD_COL_ESC,
	.row_esc = JAINA_KEYBOARD_ROW_ESC,
	.col_key_h = JAINA_KEYBOARD_COL_KEY_H,
	.row_key_h = JAINA_KEYBOARD_ROW_KEY_H,
	.col_key_r = JAINA_KEYBOARD_COL_KEY_R,
	.row_key_r = JAINA_KEYBOARD_ROW_KEY_R,
	.col_left_alt = JAINA_KEYBOARD_COL_LEFT_ALT,
	.row_left_alt = JAINA_KEYBOARD_ROW_LEFT_ALT,
	.col_refresh = JAINA_KEYBOARD_COL_REFRESH,
	.row_refresh = JAINA_KEYBOARD_ROW_REFRESH,
	.col_right_alt = JAINA_KEYBOARD_COL_RIGHT_ALT,
	.row_right_alt = JAINA_KEYBOARD_ROW_RIGHT_ALT,
	.col_left_shift = JAINA_KEYBOARD_COL_LEFT_SHIFT,
	.row_left_shift = JAINA_KEYBOARD_ROW_LEFT_SHIFT,
};

static int keyboard_choose(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(KB_MATRIX, &val);

	return val;
}

struct boot_key_entry boot_key_list[] = {
	[BOOT_KEY_ESC] = { JAINA_KEYBOARD_COL_ESC, JAINA_KEYBOARD_ROW_ESC },
	[BOOT_KEY_DOWN_ARROW] = { JAINA_KEYBOARD_COL_DOWN,
				  JAINA_KEYBOARD_ROW_DOWN },
	[BOOT_KEY_LEFT_SHIFT] = { JAINA_KEYBOARD_COL_LEFT_SHIFT,
				  JAINA_KEYBOARD_ROW_LEFT_SHIFT },
	[BOOT_KEY_REFRESH] = { JAINA_KEYBOARD_COL_REFRESH,
			       JAINA_KEYBOARD_ROW_REFRESH },
};
BUILD_ASSERT(ARRAY_SIZE(boot_key_list) == BOOT_KEY_COUNT);

static void key_choose(void)
{
	if (keyboard_choose() == 1) {
		key_typ.col_right_alt = JAINA_KEYBOARD2_COL_RIGHT_ALT;
		key_typ.row_right_alt = JAINA_KEYBOARD2_ROW_RIGHT_ALT;
		CPRINTS("keyboard_choose JP");
	} else {
		CPRINTS("keyboard_choose US");
	}
}

static void board_init(void)
{
	key_choose();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_PRE_DEFAULT);
