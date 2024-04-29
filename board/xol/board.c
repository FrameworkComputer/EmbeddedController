/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

const static struct mp2964_reg_val rail_a[] = {
	{ 0x28, 0x000c }, { 0x29, 0x0002 }, { 0x2c, 0x0384 }, { 0x38, 0x0060 },
	{ 0x3c, 0x00d1 }, { 0x3d, 0x2b01 }, { 0x3f, 0xe883 }, { 0x40, 0x034d },
	{ 0x41, 0x0153 }, { 0x42, 0x014d }, { 0x44, 0x0053 }, { 0x45, 0x0053 },
	{ 0x46, 0x00d0 }, { 0x48, 0x0151 }, { 0x4d, 0xe13f }, { 0x53, 0x0050 },
	{ 0x60, 0x64b0 }, { 0x62, 0x0cb4 }, { 0x96, 0x1e05 }, { 0xd2, 0x00d0 },
	{ 0xd4, 0x0063 }, { 0xd6, 0x003f }, { 0xd8, 0x002d }, { 0xe0, 0x0012 },
	{ 0xe2, 0x00d0 }, { 0xe8, 0x009a }, { 0xe9, 0x009a }, { 0xea, 0x009a },
	{ 0xeb, 0x009a }, { 0xef, 0x00b3 }, { 0xf0, 0x00b3 },
};
const static struct mp2964_reg_val rail_b[] = {
	{ 0x28, 0x000c }, { 0x29, 0x0001 }, { 0x2c, 0x032b }, { 0x38, 0x0038 },
	{ 0x3c, 0x00d1 }, { 0x3d, 0x2b01 }, { 0x3f, 0xe883 }, { 0x40, 0x034d },
	{ 0x41, 0x0153 }, { 0x42, 0x014d }, { 0x44, 0x0053 }, { 0x45, 0x0053 },
	{ 0x46, 0x00d0 }, { 0x4d, 0xe13f }, { 0x53, 0x0028 }, { 0x60, 0x32b0 },
	{ 0x62, 0x0cb4 }, { 0x96, 0x1e05 },
};

static void mp2964_on_startup(void)
{
	static int chip_updated;
	int status;

	if (chip_updated)
		return;

	CPRINTF("[mp2964] attempting to tune MP2964\n");

	status = mp2964_tune(rail_a, ARRAY_SIZE(rail_a), rail_b,
			     ARRAY_SIZE(rail_b));

	if (status == EC_SUCCESS) {
		chip_updated = 1;
		CPRINTF("[mp2964] mp2964 is already updated\n");
	} else
		CPRINTF("[mp2964] try to tune MP2964 (%d)\n", status);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, mp2964_on_startup, HOOK_PRIO_FIRST);

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
	.capabilities = KEYBD_CAP_FUNCTION_KEYS | KEYBD_CAP_SCRNLOCK_KEY |
			KEYBD_CAP_NUMERIC_KEYPAD | KEYBD_CAP_ASSISTANT_KEY,
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
