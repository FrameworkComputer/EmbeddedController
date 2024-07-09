/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Vivali Keyboard code for Chrome EC */

#include "builtin/assert.h"
#include "ec_commands.h"
#include "gpio.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"

#include <hooks.h>
#include <host_command.h>
#include <util.h>

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)

/*
 * Row Column info for Top row keys T1 - T15. This has been sourced from
 * go/vivaldi-matrix (internal link for vivaldi scan matrix spec).
 */
__overridable const struct key {
	uint8_t row;
	uint8_t col;
} vivaldi_keys[] = {
	{ .row = 0, .col = 2 }, /* T1 */
	{ .row = 3, .col = 2 }, /* T2 */
	{ .row = 2, .col = 2 }, /* T3 */
	{ .row = 1, .col = 2 }, /* T4 */
	{ .row = 3, .col = 4 }, /* T5 */
	{ .row = 2, .col = 4 }, /* T6 */
	{ .row = 1, .col = 4 }, /* T7 */
	{ .row = 2, .col = 9 }, /* T8 */
	{ .row = 1, .col = 9 }, /* T9 */
	{ .row = 0, .col = 4 }, /* T10 */
	{ .row = 0, .col = 1 }, /* T11 */
	{ .row = 1, .col = 5 }, /* T12 */
	{ .row = 3, .col = 5 }, /* T13 */
	{ .row = 0, .col = 9 }, /* T14 */
	{ .row = 0, .col = 11 }, /* T15 */
};
BUILD_ASSERT(ARRAY_SIZE(vivaldi_keys) == MAX_TOP_ROW_KEYS);

/* Scancodes for top row action keys */
static const uint16_t action_scancodes[] = {
	[TK_BACK] = SCANCODE_BACK,
	[TK_FORWARD] = SCANCODE_FORWARD,
	[TK_REFRESH] = SCANCODE_REFRESH,
	[TK_FULLSCREEN] = SCANCODE_FULLSCREEN,
	[TK_OVERVIEW] = SCANCODE_OVERVIEW,
	[TK_VOL_MUTE] = SCANCODE_VOLUME_MUTE,
	[TK_VOL_DOWN] = SCANCODE_VOLUME_DOWN,
	[TK_VOL_UP] = SCANCODE_VOLUME_UP,
	[TK_PLAY_PAUSE] = SCANCODE_PLAY_PAUSE,
	[TK_NEXT_TRACK] = SCANCODE_NEXT_TRACK,
	[TK_PREV_TRACK] = SCANCODE_PREV_TRACK,
	[TK_SNAPSHOT] = SCANCODE_SNAPSHOT,
	[TK_BRIGHTNESS_DOWN] = SCANCODE_BRIGHTNESS_DOWN,
	[TK_BRIGHTNESS_UP] = SCANCODE_BRIGHTNESS_UP,
	[TK_KBD_BKLIGHT_DOWN] = SCANCODE_KBD_BKLIGHT_DOWN,
	[TK_KBD_BKLIGHT_UP] = SCANCODE_KBD_BKLIGHT_UP,
	[TK_PRIVACY_SCRN_TOGGLE] = SCANCODE_PRIVACY_SCRN_TOGGLE,
	[TK_MICMUTE] = SCANCODE_MICMUTE,
	[TK_KBD_BKLIGHT_TOGGLE] = SCANCODE_KBD_BKLIGHT_TOGGLE,
	[TK_MENU] = SCANCODE_MENU,
	[TK_DICTATE] = SCANCODE_DICTATE,
	[TK_ACCESSIBILITY] = SCANCODE_ACCESSIBILITY,
	[TK_DONOTDISTURB] = SCANCODE_DONOTDISTURB,
};
BUILD_ASSERT(ARRAY_SIZE(action_scancodes) == TK_COUNT);

static const struct ec_response_keybd_config *vivaldi_keybd;

static enum ec_status
get_vivaldi_keybd_config(struct host_cmd_handler_args *args)
{
	struct ec_response_keybd_config *resp = args->response;

	if (vivaldi_keybd && vivaldi_keybd->num_top_row_keys) {
		memcpy(resp, vivaldi_keybd, sizeof(*resp));
		args->response_size = sizeof(*resp);
		return EC_RES_SUCCESS;
	}
	return EC_RES_ERROR;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_KEYBD_CONFIG, get_vivaldi_keybd_config,
		     EC_VER_MASK(0));

#ifdef CONFIG_KEYBOARD_CUSTOMIZATION

/*
 * Boards selecting CONFIG_KEYBOARD_CUSTOMIZATION are likely to not
 * want vivaldi code messing with their customized keyboards.
 */
__overridable const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return NULL;
}

#else

static const struct ec_response_keybd_config default_keybd = {
	/* Default Chromeos keyboard config */
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_FORWARD,		/* T2 */
		TK_REFRESH,		/* T3 */
		TK_FULLSCREEN,		/* T4 */
		TK_OVERVIEW,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	/* No function keys, no numeric keypad, has screenlock key */
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__overridable const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &default_keybd;
}

#endif /* CONFIG_KEYBOARD_CUSTOMIZATION */

static void vivaldi_init(void)
{
	uint8_t i;

	/* Allow the boards to change the keyboard config */
	vivaldi_keybd = board_vivaldi_keybd_config();

	if (!vivaldi_keybd || !vivaldi_keybd->num_top_row_keys) {
		CPUTS("VIVALDI keybd disabled on board request");
		return;
	}

	CPRINTS("VIVALDI: Num top row keys = %u",
		vivaldi_keybd->num_top_row_keys);

	if (vivaldi_keybd->num_top_row_keys > MAX_TOP_ROW_KEYS ||
	    vivaldi_keybd->num_top_row_keys < MIN_TOP_ROW_KEYS) {
		CPRINTS("VIVALDI: Error! num_top_row_keys=%u, disabled vivaldi",
			vivaldi_keybd->num_top_row_keys);
		vivaldi_keybd = NULL;
		return;
	}

	for (i = 0; i < ARRAY_SIZE(vivaldi_keys); i++) {
		uint8_t row, col, *mask;
		enum action_key key;

		row = vivaldi_keys[i].row;
		col = vivaldi_keys[i].col;

		if (col >= keyboard_cols || row >= KEYBOARD_ROWS) {
			CPRINTS("VIVALDI: Bad (row,col) for T-%u: (%u,%u)", i,
				row, col);
			ASSERT(false);
		}

		mask = &keyscan_config.actual_key_mask[col];

		/*
		 * Potentially indexing past meaningful data,
		 * but we bounds check it below.
		 */
		key = vivaldi_keybd->action_keys[i];

		if (i < vivaldi_keybd->num_top_row_keys && key != TK_ABSENT) {
			/* Enable the mask */
			*mask |= BIT(row);

			/* Populate the scancode */
			set_scancode_set2(row, col, action_scancodes[key]);
			CPRINTS("VIVALDI key-%u (r-%u, c-%u) = scancode-%X", i,
				row, col, action_scancodes[key]);

			if (key == TK_VOL_UP)
				set_vol_up_key(row, col);
		}
	}
}
DECLARE_HOOK(HOOK_INIT, vivaldi_init, HOOK_PRIO_DEFAULT);
