/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hid_vivaldi.h"
#include "hooks.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(usb_hid_vivaldi, LOG_LEVEL_INF);

/* Supported function key range */
#define HID_F1 0x3a
#define HID_F12 0x45
#define HID_F13 0x68
#define HID_F15 0x6a

struct action_key_config {
	uint32_t mask; /* bit position of usb_hid_keyboard_report.top_row */
	uint32_t usage; /*usage ID */
};

static const struct action_key_config action_key[] = {
	[TK_BACK] = { .mask = BIT(0), .usage = 0x000C0224 },
	[TK_FORWARD] = { .mask = BIT(1), .usage = 0x000C0225 },
	[TK_REFRESH] = { .mask = BIT(2), .usage = 0x000C0227 },
	[TK_FULLSCREEN] = { .mask = BIT(3), .usage = 0x000C0232 },
	[TK_OVERVIEW] = { .mask = BIT(4), .usage = 0x000C029F },
	[TK_BRIGHTNESS_DOWN] = { .mask = BIT(5), .usage = 0x000C0070 },
	[TK_BRIGHTNESS_UP] = { .mask = BIT(6), .usage = 0x000C006F },
	[TK_VOL_MUTE] = { .mask = BIT(7), .usage = 0x000C00E2 },
	[TK_VOL_DOWN] = { .mask = BIT(8), .usage = 0x000C00EA },
	[TK_VOL_UP] = { .mask = BIT(9), .usage = 0x000C00E9 },
	[TK_SNAPSHOT] = { .mask = BIT(10), .usage = 0x00070046 },
	[TK_PRIVACY_SCRN_TOGGLE] = { .mask = BIT(11), .usage = 0x000C02D0 },
	[TK_KBD_BKLIGHT_DOWN] = { .mask = BIT(12), .usage = 0x000C007A },
	[TK_KBD_BKLIGHT_UP] = { .mask = BIT(13), .usage = 0x000C0079 },
	[TK_PLAY_PAUSE] = { .mask = BIT(14), .usage = 0x000C00CD },
	[TK_NEXT_TRACK] = { .mask = BIT(15), .usage = 0x000C00B5 },
	[TK_PREV_TRACK] = { .mask = BIT(16), .usage = 0x000C00B6 },
	[TK_KBD_BKLIGHT_TOGGLE] = { .mask = BIT(17), .usage = 0x000C007C },
	[TK_MICMUTE] = { .mask = BIT(18), .usage = 0x000B002F },
};

/* TK_* is 1-indexed, so the next bit is at ARRAY_SIZE(action_key) - 1 */
static const int SLEEP_KEY_MASK = BIT(ARRAY_SIZE(action_key) - 1);

static const struct ec_response_keybd_config *config;

__overridable const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return NULL;
}

uint32_t vivaldi_convert_function_key(int keycode)
{
	/* zero-based function key index (e.g. F1 -> 0) */
	int index;

	if (!config) {
		return 0;
	}
	if (IN_RANGE(keycode, HID_F1, HID_F12))
		index = keycode - HID_F1;
	else if (IN_RANGE(keycode, HID_F13, HID_F15))
		index = keycode - HID_F13 + 12;
	else
		return 0; /* not a function key */

	/* convert F13 to Sleep */
	if (index == 12 && (config->capabilities & KEYBD_CAP_SCRNLOCK_KEY))
		return SLEEP_KEY_MASK;

	if (index >= config->num_top_row_keys ||
	    config->action_keys[index] == TK_ABSENT)
		return 0; /* not mapped */
	return action_key[config->action_keys[index]].mask;
}

int32_t get_vivaldi_feature_report(uint8_t *data)
{
	if (!data || !config) {
		return 0;
	}

	for (int i = 0; i < config->num_top_row_keys; i++) {
		int key = config->action_keys[i];

		memcpy(data + i * sizeof(uint32_t), &action_key[key].usage,
		       sizeof(uint32_t));
	}
	return config->num_top_row_keys * sizeof(uint32_t);
}

static void hid_vivaldi_init(void)
{
	config = board_vivaldi_keybd_config();
	if (!config || !config->num_top_row_keys) {
		LOG_ERR("failed to load vivaldi keyboard configuration");
		config = NULL;
		return;
	}

	if (config->num_top_row_keys > MAX_TOP_ROW_KEYS ||
	    config->num_top_row_keys < MIN_TOP_ROW_KEYS) {
		LOG_ERR("invaild top row keys number");
		config = NULL;
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, hid_vivaldi_init, HOOK_PRIO_DEFAULT - 1);
