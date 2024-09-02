/* Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "hooks.h"

#include "board_host_command.h"
#include "common.h"
#include "chipset.h"
#include "customized_shared_memory.h"
#include "factory.h"
#include "keyboard_customization.h"
#include "keyboard_8042.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "keyboard_backlight.h"
#include "pwm.h"
#include "hooks.h"
#include "system.h"
#include "hid_device.h"
#include "driver/ioexpander/it8801.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

static void disable_keyscan(void)
{
	/**
	 * We don't want to read the keyboard before turning on the power.
	 * Disable the keyboard scan function to avoid the watchdog.
	 */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_DISCONNECT);
}
DECLARE_HOOK(HOOK_INIT_EARLY, disable_keyscan, HOOK_PRIO_DEFAULT);

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 80,
	.debounce_down_us = 20 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xa1,
		0xff, 0xff, 0x01, 0xff,	0xff, 0x40, 0x0a, 0x40,
		0x01, 0xc4 /* full set */
	},
};

uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{0x0000, 0x0000, 0x0000, 0xE01F, 0x0000, 0x0000, 0x0000, 0x0000},
	{0x0078, 0x0076, 0x000D, 0x000E, 0x001C, 0x0016, 0x001A, 0x003C},
	{0x0005, 0x000C, 0x0004, 0x0006, 0x0023, 0x0041, 0x0026, 0x0043},
	{0x0032, 0x0034, 0x002C, 0x002E, 0x002B, 0x0049, 0x0025, 0x0044},
	{0x0009, 0x0083, 0x000B, 0x001B, 0x0003, 0x004A, 0x001E, 0x004D},
	{0x0031, 0x0007, 0x005B, 0x0000, 0x0042, 0x0021, 0x003E, 0x0015},
	{0x0051, 0x0033, 0x0035, 0x004E, 0x003B, 0x0029, 0x0045, 0x001D},
	{0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059},
	{0x0055, 0x0052, 0x0054, 0x0036, 0x004C, 0x0022, 0x003D, 0x0024},
	{0xE06C, 0x0001, 0xE071, 0x0000, 0x004B, 0x002A, 0x0046, 0x002D},
	{0xE011, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
	{0x0000, 0x0066, 0x000A, 0x005D, 0x005A, 0x003A, 0xE072, 0xE075},
	{0x0000, 0x0064, 0xE07D, 0x0067, 0xE069, 0xE07A, 0xE074, 0xE06B},
	{0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0011, 0x0000},
	{0x0000, 0x0014, 0x0000, 0xE014, 0x0000, 0x0000, 0x0000, 0x0000},
	{0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0058, 0x0000},
	{0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
	{0x0000, 0x0000, 0x007D, 0x0000, 0x0000, 0x0000, 0x005D, 0x0061},
};

void board_caps_led_control(int data)
{
}

/* KSO mapping for discrete keyboard */
__override const uint8_t it8801_kso_mapping[] = {
	21, 20, 5, 19, 18, 17, 11, 14, 13, 15, 16, 12, 0, 2, 1, 3, 6, 4,
};
BUILD_ASSERT(ARRAY_SIZE(it8801_kso_mapping) == KEYBOARD_COLS_MAX);

/* KSI mapping for discrete keyboard */
__override int it8801_ksi_mapping_transfer(int ksi_val)
{
	if (ksi_val == 255)
		return ksi_val;

	int ksi_output = 255;

	for (int i = 0; i < KEYBOARD_ROWS; i++) {
		if (!(ksi_val & BIT(i))) {
			if (i == 0)
				ksi_output &= ~BIT(3);
			else if (i == 2)
				ksi_output &= ~BIT(4);
			else if (i == 3)
				ksi_output &= ~BIT(0);
			else if (i == 4)
				ksi_output &= ~BIT(5);
			else if (i == 5)
				ksi_output &= ~BIT(2);
			else
				ksi_output &= ~BIT(i);
		}
	}
	return ksi_output;
}

uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return scancode_set2[col][row];
	return 0;
}

void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		scancode_set2[col][row] = val;
}

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_DEBUG
static char keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{KLLI_UNKNO, KLLI_UNKNO, KLLI_L_CTR, KLLI_SEARC,
			KLLI_R_CTR, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO},
	{KLLI_F11,   KLLI_ESC,   KLLI_TAB,   '~',
			'a',        'z',        '1',        'q'},
	{KLLI_F1,    KLLI_F4,    KLLI_F3,    KLLI_F2,
			'd',        'c',        '3',        'e'},
	{'b',        'g',        't',        '5',
			'f',        'v',        '4',        'r'},
	{KLLI_F10,   KLLI_F7,    KLLI_F6,    KLLI_F5,
			's',        'x',        '2',        'w'},
	{KLLI_UNKNO, KLLI_F12,   ']',        KLLI_F13,
			'k',        ',',        '8',        'i'},
	{'n',        'h',        'y',        '6',
			'j',        'm',        '7',        'u'},
	{KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
			KLLI_UNKNO, KLLI_L_SHT, KLLI_UNKNO, KLLI_R_SHT},
	{'=',        '\'',       '[',        '-',
			';',        '/',        '0',        'p'},
	{KLLI_F14,   KLLI_F9,    KLLI_F8,    KLLI_UNKNO,
			'|',        '.',        '9',        'o'},
	{KLLI_R_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
			KLLI_UNKNO, KLLI_UNKNO, KLLI_L_ALT, KLLI_UNKNO},
	{KLLI_F15,   KLLI_B_SPC, KLLI_UNKNO, '\\',
			KLLI_ENTER, KLLI_SPACE, KLLI_DOWN,  KLLI_UP},
	{KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
			KLLI_UNKNO, KLLI_UNKNO, KLLI_RIGHT, KLLI_LEFT},
};

uint8_t get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return keycap_label[col][row];
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, uint8_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		keycap_label[col][row] = val;
}
#endif

#define FN_PRESSED BIT(0)
#define FN_LOCKED BIT(1)
static uint8_t Fn_key;
static uint32_t fn_key_table_media;
static uint32_t fn_key_table;

int fn_table_media_set(int8_t pressed, uint32_t fn_bit)
{
	if (pressed) {
		fn_key_table_media |= fn_bit;
		return true;
	} else if (!pressed && (fn_key_table_media & fn_bit)) {
		fn_key_table_media &= ~fn_bit;
		return true;
	}

	return false;
}

int fn_table_set(int8_t pressed, uint32_t fn_bit)
{
	if (pressed && (Fn_key & FN_PRESSED)) {
		fn_key_table |= fn_bit;
		return true;
	} else if (!pressed && (fn_key_table & fn_bit)) {
		fn_key_table &= ~fn_bit;
		return true;
	}

	return false;
}

void fnkey_shutdown(void)
{
	uint8_t current_kb = 0;

	if (Fn_key & FN_LOCKED) {
		current_kb |= 0x80;
	}
	system_set_bbram(SYSTEM_BBRAM_IDX_KBSTATE, current_kb);

	Fn_key &= ~FN_LOCKED;
	Fn_key &= ~FN_PRESSED;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, fnkey_shutdown, HOOK_PRIO_DEFAULT);


void fnkey_startup(void)
{
	uint8_t current_kb = 0;

	if (system_get_bbram(SYSTEM_BBRAM_IDX_KBSTATE, &current_kb) == EC_SUCCESS) {
		if (current_kb & 0x80) {
			Fn_key |= FN_LOCKED;
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, fnkey_startup, HOOK_PRIO_DEFAULT);

int hotkey_F1_F12(uint16_t *key_code, uint16_t fn, int8_t pressed)
{
	const uint16_t prss_key = *key_code;

	if (!(Fn_key & FN_LOCKED) &&
		(fn & FN_PRESSED))
		return EC_SUCCESS;
	else if (Fn_key & FN_LOCKED &&
		!(fn & FN_PRESSED) &&
		!fn_key_table_media)
		return EC_SUCCESS;
	else if (!fn_key_table_media && !pressed)
		return EC_SUCCESS;

	switch (prss_key) {
	case SCANCODE_F1:  /* SPEAKER_MUTE */
		if (fn_table_media_set(pressed, KB_FN_F1))
			*key_code = SCANCODE_VOLUME_MUTE;
		break;
	case SCANCODE_F2:  /* VOLUME_DOWN */
		if (fn_table_media_set(pressed, KB_FN_F2))
			*key_code = SCANCODE_VOLUME_DOWN;
		break;
	case SCANCODE_F3:  /* VOLUME_UP */
		if (fn_table_media_set(pressed, KB_FN_F3))
			*key_code = SCANCODE_VOLUME_UP;
		break;
	case SCANCODE_F4:  /* PREVIOUS_TRACK */
		if (fn_table_media_set(pressed, KB_FN_F4))
			*key_code = SCANCODE_PREV_TRACK;
		break;
	case SCANCODE_F5:  /* PLAY_PAUSE */
		if (fn_table_media_set(pressed, KB_FN_F5))
			*key_code = 0xe034;
		break;
	case SCANCODE_F6:  /* NEXT_TRACK */
		if (fn_table_media_set(pressed, KB_FN_F6))
			*key_code = SCANCODE_NEXT_TRACK;
		break;
	case SCANCODE_F7:  /* DIM_SCREEN */
		if (fn_table_media_set(pressed, KB_FN_F7)) {
			hid_consumer(BUTTON_ID_BRIGHTNESS_DECREMENT, pressed);
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_F8:  /* BRIGHTEN_SCREEN */
		if (fn_table_media_set(pressed, KB_FN_F8)) {
			hid_consumer(BUTTON_ID_BRIGHTNESS_INCREMENT, pressed);
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_F9:  /* EXTERNAL_DISPLAY */
		if (fn_table_media_set(pressed, KB_FN_F9)) {
			if (pressed) {
				simulate_keyboard(SCANCODE_LEFT_WIN, 1);
				simulate_keyboard(SCANCODE_P, 1);
			} else {
				simulate_keyboard(SCANCODE_P, 0);
				simulate_keyboard(SCANCODE_LEFT_WIN, 0);
			}
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_F10:  /* FLIGHT_MODE */
		if (fn_table_media_set(pressed, KB_FN_F10)) {
			hid_airplane(pressed);
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_F11:
			/*
			 * TODO this might need an
			 * extra key combo of:
			 * 0xE012 0xE07C to simulate
			 * PRINT_SCREEN
			 */
		if (fn_table_media_set(pressed, KB_FN_F11))
			*key_code = 0xE07C;
		break;
	case SCANCODE_F12:  /* Framework logo key */
		/* Media Select scan code */
		if (fn_table_media_set(pressed, KB_FN_F12))
			*key_code = 0xE050;
		break;
	default:
		return EC_SUCCESS;
	}
	return EC_SUCCESS;
}


int hotkey_special_key(uint16_t *key_code, int8_t pressed)
{
	const uint16_t prss_key = *key_code;

	switch (prss_key) {
	case SCANCODE_DELETE:  /* INSERT */
		if (fn_table_set(pressed, KB_FN_DELETE))
			*key_code = 0xe070;
		break;
	case SCANCODE_K:
		if (fn_table_set(pressed, KB_FN_K))
			*key_code = SCANCODE_SCROLL_LOCK;
		break;
	case SCANCODE_S:  /* TODO: SYSRQ */
		/*if (!fn_table_set(pressed, KB_FN_S))*/

		break;
	case SCANCODE_LEFT:  /* HOME */
		if (fn_table_set(pressed, KB_FN_LEFT))
			*key_code = 0xe06c;
		break;
	case SCANCODE_RIGHT:  /* END */
		if (fn_table_set(pressed, KB_FN_RIGHT))
			*key_code = 0xe069;
		break;
	case SCANCODE_UP:  /* PAGE_UP */
		if (fn_table_set(pressed, KB_FN_UP))
			*key_code = 0xe07d;
		break;
	case SCANCODE_DOWN:  /* PAGE_DOWN */
		if (fn_table_set(pressed, KB_FN_DOWN))
			*key_code = 0xe07a;
		break;
	default:
		return EC_SUCCESS;
	}

	return EC_SUCCESS;
}

int functional_hotkey(uint16_t *key_code, int8_t pressed)
{
	const uint16_t prss_key = *key_code;

	switch (prss_key) {
	case SCANCODE_ESC: /* FUNCTION_LOCK */
		if (fn_table_set(pressed, KB_FN_ESC)) {
			if (pressed) {
				if (Fn_key & FN_LOCKED)
					Fn_key &= ~FN_LOCKED;
				else
					Fn_key |= FN_LOCKED;
			}
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_B:
		/* BREAK_KEY */
		if (fn_table_set(pressed, KB_FN_B)) {
			if (pressed) {
				simulate_keyboard(0xe07e, 1);
				simulate_keyboard(0xe0, 1);
				simulate_keyboard(0x7e, 0);
			}
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_P:
		/* PAUSE_KEY */
		if (fn_table_set(pressed, KB_FN_P)) {
			if (pressed) {
				simulate_keyboard(0xe114, 1);
				simulate_keyboard(0x77, 1);
				simulate_keyboard(0xe1, 1);
				simulate_keyboard(0x14, 0);
				simulate_keyboard(0x77, 0);
			}
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_SPACE:
		if (fn_table_set(pressed, KB_FN_SPACE)) {
			/* Sunflower not have keyboard backlight*/
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	}
	return EC_SUCCESS;
}

enum ec_error_list keyboard_scancode_callback(uint16_t *make_code,
					      int8_t pressed)
{
	const uint16_t pressed_key = *make_code;
	int r = 0;

	if (factory_status())
		return EC_SUCCESS;

	if (pressed_key == SCANCODE_FN && pressed) {
		Fn_key |= FN_PRESSED;
		return EC_ERROR_UNIMPLEMENTED;
	} else if (pressed_key == SCANCODE_FN && !pressed) {
		Fn_key &= ~FN_PRESSED;
		return EC_ERROR_UNIMPLEMENTED;
	}

	/*
	 * If the system still in preOS
	 * then we pass through all events without modifying them
	 * will refact BIT after dGPU merged
	 */
	if (!*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & BIT(0))
		return EC_SUCCESS;

	r = hotkey_F1_F12(make_code, Fn_key, pressed);
	if (r != EC_SUCCESS)
		return r;
	/*
	 * If the function key is not held then
	 * we pass through all events without modifying them
	 * but if last time have press FN still need keep that
	 */
	if (!(Fn_key & FN_PRESSED) && !fn_key_table)
		return EC_SUCCESS;

	r = hotkey_special_key(make_code, pressed);
	if (r != EC_SUCCESS)
		return r;

	r = functional_hotkey(make_code, pressed);
	if (r != EC_SUCCESS)
		return r;

	return EC_SUCCESS;
}
