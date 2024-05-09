/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/mp2964.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "throttle_ap.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

static const struct ec_response_keybd_config xol_kb = {
	.num_top_row_keys = 14,
	.action_keys = {
		TK_BACK,                /* T1 */
		TK_REFRESH,             /* T2 */
		TK_FULLSCREEN,          /* T3 */
		TK_OVERVIEW,            /* T4 */
		TK_SNAPSHOT,            /* T5 */
		TK_BRIGHTNESS_DOWN,     /* T6 */
		TK_BRIGHTNESS_UP,       /* T7 */
		TK_KBD_BKLIGHT_DOWN,    /* T8 */
		TK_KBD_BKLIGHT_UP,      /* T9 */
		TK_PLAY_PAUSE,          /* T10 */
		TK_MICMUTE,             /* T11 */
		TK_VOL_MUTE,            /* T12 */
		TK_VOL_DOWN,            /* T13 */
		TK_VOL_UP,              /* T14 */
	},
	.capabilities = KEYBD_CAP_FUNCTION_KEYS | KEYBD_CAP_SCRNLOCK_KEY |
			KEYBD_CAP_NUMERIC_KEYPAD,
};

static const struct ec_response_keybd_config xol_kb2 = {
	.num_top_row_keys = 15,
	.action_keys = {
		TK_BACK,                /* T1 */
		TK_REFRESH,             /* T2 */
		TK_FULLSCREEN,          /* T3 */
		TK_OVERVIEW,            /* T4 */
		TK_SNAPSHOT,            /* T5 */
		TK_BRIGHTNESS_DOWN,     /* T6 */
		TK_BRIGHTNESS_UP,       /* T7 */
		TK_KBD_BKLIGHT_DOWN,    /* T8 */
		TK_KBD_BKLIGHT_UP,      /* T9 */
		TK_DICTATE,             /* T10 */
		TK_FORWARD,             /* T11 Temporary */
		TK_PLAY_PAUSE,          /* T12 */
		TK_VOL_MUTE,            /* T13 */
		TK_VOL_DOWN,            /* T14 */
		TK_VOL_UP,              /* T15 */
	},
	.capabilities = KEYBD_CAP_FUNCTION_KEYS | KEYBD_CAP_NUMERIC_KEYPAD |
			KEYBD_CAP_ASSISTANT_KEY,
};

static uint32_t board_id = (uint32_t)UINT8_MAX;
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (get_board_id() <= 2)
		return &xol_kb;
	else
		return &xol_kb2;
}

static void set_board_id(void)
{
	uint32_t cbi_val;

	/* Board ID, only need to do it once */
	if (board_id == (uint32_t)UINT8_MAX) {
		if (cbi_get_board_version(&cbi_val) != EC_SUCCESS ||
		    cbi_val > UINT8_MAX)
			CPRINTS("CBI: Read Board ID failed");
		else
			board_id = cbi_val;
		CPRINTS("Read Board ID: %u", board_id);
	}
}

uint8_t board_get_finch_version(void)
{
	set_board_id();

	if (board_id <= 2)
		return 0x23;
	else
		return 0x30;
}

__override struct key {
	uint8_t row;
	uint8_t col;
} vivaldi_keys[] = {
	{ .row = 0, .col = 2 }, /* T1 */
	{ .row = 3, .col = 2 }, /* T2 */
	{ .row = 2, .col = 2 }, /* T3 */
	{ .row = 1, .col = 2 }, /* T4 */
	{ .row = 4, .col = 4 }, /* T5 */
	{ .row = 2, .col = 4 }, /* T6 */
	{ .row = 1, .col = 4 }, /* T7 */
	{ .row = 2, .col = 11 }, /* T8 */
	{ .row = 1, .col = 9 }, /* T9 */
	{ .row = 0, .col = 4 }, /* T10 */
	{ .row = 0, .col = 1 }, /* T11 */
	{ .row = 1, .col = 5 }, /* T12 */
	{ .row = 3, .col = 5 }, /* T13 */
	{ .row = 0, .col = 11 }, /* T14 */
	{ .row = 0, .col = 12 }, /* T15 */
};
BUILD_ASSERT(ARRAY_SIZE(vivaldi_keys) == MAX_TOP_ROW_KEYS);

static void board_init(void)
{
	if (get_board_id() <= 2) {
		vivaldi_keys[4].row = 3;
		vivaldi_keys[4].col = 4;
		vivaldi_keys[7].row = 2;
		vivaldi_keys[7].col = 9;
		vivaldi_keys[13].row = 0;
		vivaldi_keys[13].col = 9;
		vivaldi_keys[14].row = 0;
		vivaldi_keys[14].col = 12;
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_PRE_DEFAULT);

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	charger_set_input_current_limit(
		CHARGER_SOLO,
		charge_ma * (100 - CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT) /
			100);
}
