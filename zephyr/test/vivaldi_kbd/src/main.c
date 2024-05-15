/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"

#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <dt-bindings/vivaldi_kbd.h>
#include <ec_commands.h>
#include <hooks.h>
#include <host_command.h>
#include <keyboard_8042_sharedlib.h>
#include <keyboard_scan.h>

static struct {
	uint16_t codes[KEYBOARD_ROWS][KEYBOARD_COLS_MAX];
	int call_count;
} set2_test;

void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	set2_test.codes[row][col] = val;
	set2_test.call_count++;
}

static struct {
	uint16_t row;
	uint16_t col;
	int call_count;
} vol_up_key;

void set_vol_up_key(uint8_t row, uint8_t col)
{
	vol_up_key.row = row;
	vol_up_key.col = col;
	vol_up_key.call_count++;
}

struct keyboard_scan_config keyscan_config;

#if CONFIG_VIVALDI_KBD_TEST_USE_IDX
int8_t board_vivaldi_keybd_idx(void)
{
	if (IS_ENABLED(CONFIG_VIVALDI_KBD_CBI_RACE_TEST)) {
		uint32_t val;
		int ret;

		ret = cros_cbi_get_fw_config(FW_CONFIG_FIELD_1, &val);
		zassert_equal(ret, 0);
		zassert_equal(val, 1);
	}

	return CONFIG_VIVALDI_KBD_TEST_IDX_VALUE;
}
#endif

#if CONFIG_VIVALDI_KBD_CBI_RACE_TEST
static int early_cbi_test(void)
{
	uint32_t val;
	int ret;

	ret = cros_cbi_get_fw_config(FW_CONFIG_FIELD_1, &val);
	TC_PRINT("early cros_cbi_get_fw_config: %d\n", ret);
	zassert_equal(ret, -EINVAL);

	return 0;
}
SYS_INIT(early_cbi_test, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
#endif

ZTEST(vivaldi_kbd, test_matching_codes)
{
	/* Ensure that devicetree binding codes are in sync with the common ones
	 */
	zassert_equal(TK_ABSENT, VIVALDI_TK_ABSENT);
	zassert_equal(TK_BACK, VIVALDI_TK_BACK);
	zassert_equal(TK_FORWARD, VIVALDI_TK_FORWARD);
	zassert_equal(TK_REFRESH, VIVALDI_TK_REFRESH);
	zassert_equal(TK_FULLSCREEN, VIVALDI_TK_FULLSCREEN);
	zassert_equal(TK_OVERVIEW, VIVALDI_TK_OVERVIEW);
	zassert_equal(TK_BRIGHTNESS_DOWN, VIVALDI_TK_BRIGHTNESS_DOWN);
	zassert_equal(TK_BRIGHTNESS_UP, VIVALDI_TK_BRIGHTNESS_UP);
	zassert_equal(TK_VOL_MUTE, VIVALDI_TK_VOL_MUTE);
	zassert_equal(TK_VOL_DOWN, VIVALDI_TK_VOL_DOWN);
	zassert_equal(TK_VOL_UP, VIVALDI_TK_VOL_UP);
	zassert_equal(TK_SNAPSHOT, VIVALDI_TK_SNAPSHOT);
	zassert_equal(TK_PRIVACY_SCRN_TOGGLE, VIVALDI_TK_PRIVACY_SCRN_TOGGLE);
	zassert_equal(TK_KBD_BKLIGHT_DOWN, VIVALDI_TK_KBD_BKLIGHT_DOWN);
	zassert_equal(TK_KBD_BKLIGHT_UP, VIVALDI_TK_KBD_BKLIGHT_UP);
	zassert_equal(TK_PLAY_PAUSE, VIVALDI_TK_PLAY_PAUSE);
	zassert_equal(TK_NEXT_TRACK, VIVALDI_TK_NEXT_TRACK);
	zassert_equal(TK_PREV_TRACK, VIVALDI_TK_PREV_TRACK);
	zassert_equal(TK_KBD_BKLIGHT_TOGGLE, VIVALDI_TK_KBD_BKLIGHT_TOGGLE);
	zassert_equal(TK_MICMUTE, VIVALDI_TK_MICMUTE);
	zassert_equal(TK_MENU, VIVALDI_TK_MENU);

	zassert_equal(KEYBD_CAP_FUNCTION_KEYS, VIVALDI_KEYBD_CAP_FUNCTION_KEYS);
	zassert_equal(KEYBD_CAP_NUMERIC_KEYPAD,
		      VIVALDI_KEYBD_CAP_NUMERIC_KEYPAD);
	zassert_equal(KEYBD_CAP_SCRNLOCK_KEY, VIVALDI_KEYBD_CAP_SCRNLOCK_KEY);
}

#if !CONFIG_VIVALDI_KBD_TEST_USE_IDX || CONFIG_VIVALDI_KBD_TEST_IDX_VALUE == 0
static const enum ec_status hc_resp_expect = EC_RES_SUCCESS;
static const uint8_t action_keys_expect[] = {
	TK_BACK,     TK_FORWARD,	 TK_REFRESH,	   TK_FULLSCREEN,
	TK_OVERVIEW, TK_BRIGHTNESS_DOWN, TK_BRIGHTNESS_UP, TK_VOL_MUTE,
	TK_VOL_DOWN, TK_VOL_UP,
};
static const uint32_t capabilities_expect = KEYBD_CAP_SCRNLOCK_KEY;
static const uint8_t actual_key_mask_expect[] = {
	0,
	0,
	BIT(0) | BIT(1) | BIT(2) | BIT(3),
	0,
	BIT(0) | BIT(1) | BIT(2) | BIT(3),
	0,
	0,
	0,
	0,
	BIT(1) | BIT(2),
	0,
	0,
};
uint16_t scancodes_expect[KEYBOARD_ROWS][KEYBOARD_COLS_MAX] = {
	[0] = {
		[2] = SCANCODE_BACK,
		[4] = SCANCODE_VOLUME_UP,
	},
	[1] = {
		[2] = SCANCODE_FULLSCREEN,
		[4] = SCANCODE_BRIGHTNESS_UP,
		[9] = SCANCODE_VOLUME_DOWN,
	},
	[2] = {
		[2] = SCANCODE_REFRESH,
		[4] = SCANCODE_BRIGHTNESS_DOWN,
		[9] = SCANCODE_VOLUME_MUTE,
	},
	[3] = {
		[2] = SCANCODE_FORWARD,
		[4] = SCANCODE_OVERVIEW,
	},
};
#elif CONFIG_VIVALDI_KBD_TEST_IDX_VALUE == 1
static const enum ec_status hc_resp_expect = EC_RES_SUCCESS;
static const uint8_t action_keys_expect[] = {
	TK_PLAY_PAUSE, TK_NEXT_TRACK, TK_PREV_TRACK,
	TK_ABSENT,     TK_ABSENT,     TK_ABSENT,
	TK_ABSENT,     TK_ABSENT,     TK_KBD_BKLIGHT_TOGGLE,
	TK_MICMUTE,    TK_MENU,
};
static const uint32_t capabilities_expect = KEYBD_CAP_SCRNLOCK_KEY |
					    KEYBD_CAP_NUMERIC_KEYPAD |
					    KEYBD_CAP_SCRNLOCK_KEY;
static const uint8_t actual_key_mask_expect[] = {
	0, BIT(0), BIT(0) | BIT(2) | BIT(3), 0, BIT(0), 0, 0, 0, 0, BIT(1),
	0, 0,
};
uint16_t scancodes_expect[KEYBOARD_ROWS][KEYBOARD_COLS_MAX] = {
	[0] = {
		[1] = SCANCODE_MENU,
		[2] = SCANCODE_PLAY_PAUSE,
		[4] = SCANCODE_MICMUTE,
	},
	[1] = {
		[9] = SCANCODE_KBD_BKLIGHT_TOGGLE,
	},
	[2] = {
		[2] = SCANCODE_PREV_TRACK,
	},
	[3] = {
		[2] = SCANCODE_NEXT_TRACK,
	},
};
#elif CONFIG_VIVALDI_KBD_TEST_IDX_VALUE == -1
static const enum ec_status hc_resp_expect = EC_RES_ERROR;
static const uint8_t action_keys_expect[0];
static const uint32_t capabilities_expect;
static const uint8_t actual_key_mask_expect[12];
uint16_t scancodes_expect[KEYBOARD_ROWS][KEYBOARD_COLS_MAX];
#else
#error "invalid config"
#endif

#define ACTION_KEYS_EXPECT_SIZE ARRAY_SIZE(action_keys_expect)

ZTEST(vivaldi_kbd, test_get_vivaldi_keybd_config)
{
	enum ec_status ret;
	struct ec_response_keybd_config resp;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_GET_KEYBD_CONFIG, 0, resp);

	ret = host_command_process(&args);

	zassert_equal(ret, hc_resp_expect);
	zassert_equal(resp.num_top_row_keys, ACTION_KEYS_EXPECT_SIZE);

	for (int i = 0; i < ACTION_KEYS_EXPECT_SIZE; i++) {
		uint8_t got, expect;

		expect = action_keys_expect[i];
		got = resp.action_keys[i];

		TC_PRINT("check action_keys %d: got=%x expect=%x\n", i, got,
			 expect);

		zassert_equal(got, expect);
	}

	zassert_equal(resp.capabilities, capabilities_expect);
}

ZTEST(vivaldi_kbd, test_actual_key_mask)
{
	for (int i = 0; i < ARRAY_SIZE(actual_key_mask_expect); i++) {
		uint8_t got, expect;

		expect = actual_key_mask_expect[i];
		got = keyscan_config.actual_key_mask[i];

		TC_PRINT("check actual_key_mask %d: got=%x expect=%x\n", i, got,
			 expect);

		zassert_equal(got, expect);
	}
}

ZTEST(vivaldi_kbd, test_set2_codes)
{
	int keycodes = 0;

	for (int r = 0; r < KEYBOARD_ROWS; r++) {
		for (int c = 0; c < KEYBOARD_COLS_MAX; c++) {
			uint8_t got, expect;

			expect = scancodes_expect[r][c];
			got = set2_test.codes[r][c];

			TC_PRINT("check set2_codes %d,%d: got=%x expect=%x\n",
				 r, c, got, expect);

			zassert_equal(got, expect);

			if (got > 0) {
				keycodes++;
			}
		}
	}

	zassert_equal(set2_test.call_count, keycodes);
}

ZTEST(vivaldi_kbd, test_vol_up_key)
{
#if !CONFIG_VIVALDI_KBD_TEST_USE_IDX || CONFIG_VIVALDI_KBD_TEST_IDX_VALUE == 0
	zassert_equal(vol_up_key.row, 0);
	zassert_equal(vol_up_key.col, 4);
	zassert_equal(vol_up_key.call_count, 1);
#else
	zassert_equal(vol_up_key.call_count, 0);
#endif
}

static void *vivaldi_setup(const void *state)
{
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(vivaldi_kbd, NULL, vivaldi_setup, NULL, NULL, NULL);
