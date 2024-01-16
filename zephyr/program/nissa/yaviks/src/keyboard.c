/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "ec_commands.h"
#include "gpio_it8xxx2.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"
#include "timer.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static const struct ec_response_keybd_config yaviks_kb_w_kb_light = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_KBD_BKLIGHT_TOGGLE,	/* T8 */
		TK_PLAY_PAUSE,		/* T9 */
		TK_MICMUTE,		/* T10 */
		TK_VOL_MUTE,		/* T11 */
		TK_VOL_DOWN,		/* T12 */
		TK_VOL_UP,		/* T13 */
	},
	.capabilities = KEYBD_CAP_NUMERIC_KEYPAD,
};

static const struct ec_response_keybd_config yaviks_kb_wo_kb_light = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_PLAY_PAUSE,		/* T8 */
		TK_MICMUTE,		/* T9 */
		TK_VOL_MUTE,		/* T10 */
		TK_VOL_DOWN,		/* T11 */
		TK_VOL_UP,		/* T12 */
		TK_MENU,		/* T13 */
	},
	.capabilities = KEYBD_CAP_NUMERIC_KEYPAD,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	uint32_t val;

	cros_cbi_get_fw_config(FW_KB_BACKLIGHT, &val);

	if (val == FW_KB_BACKLIGHT_OFF)
		return &yaviks_kb_wo_kb_light;
	else
		return &yaviks_kb_w_kb_light;
}

/*
 * Keyboard layout decided by FW config.
 */
static void kb_layout_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_LAYOUT, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_KB_LAYOUT);
		return;
	}
	/*
	 * If keyboard is US2(FW_KB_LAYOUT_US2), we need translate right ctrl
	 * to backslash(\|) key.
	 */
	if (val == FW_KB_LAYOUT_US2)
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);

/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 30 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 },	  { GPIO_KSOH, 4 }, { GPIO_KSOH, 0 }, { GPIO_KSOH, 1 },
	{ GPIO_KSOH, 3 }, { GPIO_KSOH, 2 }, { -1, -1 },	      { -1, -1 },
	{ GPIO_KSOL, 5 }, { GPIO_KSOL, 6 }, { -1, -1 },	      { GPIO_KSOL, 3 },
	{ GPIO_KSOL, 2 }, { GPIO_KSI, 0 },  { GPIO_KSOL, 1 }, { GPIO_KSOL, 4 },
	{ GPIO_KSI, 3 },  { GPIO_KSI, 2 },  { GPIO_KSOL, 0 }, { GPIO_KSI, 5 },
	{ GPIO_KSI, 4 },  { GPIO_KSOL, 7 }, { GPIO_KSI, 6 },  { GPIO_KSI, 7 },
	{ GPIO_KSI, 1 },  { -1, -1 },	    { GPIO_KSOH, 5 }, { -1, -1 },
	{ GPIO_KSOH, 6 }, { -1, -1 },	    { -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
