/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "keyboard_backlight.h"
#include "pwm.h"
#include "hooks.h"
#include "system.h"
#include "hid_device.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
		{0x0021, 0x007B, 0x0079, 0x0072, 0x007A, 0x0071, 0x0069, 0xe04A},
		{0xe071, 0xe070, 0x007D, 0xe01f, 0x006c, 0xe06c, 0xe07d, 0x0077},
		{0x0015, 0x0070, 0x00ff, 0x000D, 0x000E, 0x0016, 0x0067, 0x001c},
		{0xe011, 0x0011, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0xe05a, 0x0029, 0x0024, 0x000c, 0x0058, 0x0026, 0x0004, 0xe07a},
		{0x0022, 0x001a, 0x0006, 0x0005, 0x001b, 0x001e, 0x001d, 0x0076},
		{0x002A, 0x0032, 0x0034, 0x002c, 0x002e, 0x0025, 0x002d, 0x002b},
		{0x003a, 0x0031, 0x0033, 0x0035, 0x0036, 0x003d, 0x003c, 0x003b},
		{0x0049, 0xe072, 0x005d, 0x0044, 0x0009, 0x0046, 0x0078, 0x004b},
		{0x0059, 0x0012, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0041, 0x007c, 0x0083, 0x000b, 0x0003, 0x003e, 0x0043, 0x0042},
		{0x0013, 0x0064, 0x0075, 0x0001, 0x0051, 0x0061, 0xe06b, 0xe02f},
		{0xe014, 0x0014, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x004a, 0xe075, 0x004e, 0x0007, 0x0045, 0x004d, 0x0054, 0x004c},
		{0x0052, 0x005a, 0xe03c, 0xe069, 0x0055, 0x0066, 0x005b, 0x0023},
		{0x006a, 0x000a, 0xe074, 0xe054, 0x0000, 0x006b, 0x0073, 0x0074},
};


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

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us from 50us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 20 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0xff, 0xff, 0xff, 0x03, 0xff, 0xff, 0xff,
		0xff, 0xff, 0x03, 0xff, 0xff, 0x03, 0xff, 0xff, 0xef  /* full set */
	},
};

/*
 * TODO:
 * Azalea will change HW design at DVT
 * KBL will use PWM, for now will use GPIO control first
 */

enum backlight_brightness {
	KEYBOARD_BL_BRIGHTNESS_OFF = 0,
	KEYBOARD_BL_BRIGHTNESS_LOW = 20,
	KEYBOARD_BL_BRIGHTNESS_MED = 50,
	KEYBOARD_BL_BRIGHTNESS_HIGH = 100,
};

uint8_t bl_brightness = KEYBOARD_BL_BRIGHTNESS_OFF;

#define SCROLL_LED BIT(0)
#define NUM_LED BIT(1)
#define CAPS_LED BIT(2)
static uint8_t caps_led_status;

void board_kblight_init(void)
{
	uint8_t current_kblight = 0;

	if (system_get_bbram(SYSTEM_BBRAM_IDX_KBSTATE, &current_kblight) == EC_SUCCESS)
		kblight_set(current_kblight & 0x7F);
}

int caps_status_check(void)
{
	return caps_led_status;
}

void board_caps_led_control(int data)
{
	if (data & CAPS_LED) {
		caps_led_status = 1;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cap_led), 1);
	} else {
		caps_led_status = 0;
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cap_led), 0);
	}
}

void caps_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cap_led), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, caps_suspend, HOOK_PRIO_DEFAULT);

void caps_resume(void)
{
	if (caps_status_check())
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_cap_led), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, caps_resume, HOOK_PRIO_DEFAULT);


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

	current_kb |= kblight_get() & 0x7F;

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
	case SCANCODE_F7:  /* TODO: DIM_SCREEN */
		if (fn_table_media_set(pressed, KB_FN_F7)) {
			hid_consumer(BUTTON_ID_BRIGHTNESS_DECREMENT, pressed);
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_F8:  /* TODO: BRIGHTEN_SCREEN */
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
	case SCANCODE_F12:  /* TODO: FRAMEWORK */
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
	case SCANCODE_DELETE:  /* TODO: INSERT */
		if (fn_table_set(pressed, KB_FN_DELETE))
			*key_code = 0xe070;
		break;
	case SCANCODE_K:  /* TODO: SCROLL_LOCK */
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
	case SCANCODE_ESC: /* TODO: FUNCTION_LOCK */
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
	case SCANCODE_SPACE:	/* TODO: TOGGLE_KEYBOARD_BACKLIGHT */
		if (fn_table_set(pressed, KB_FN_SPACE)) {
			if (pressed) {
				bl_brightness = kblight_get();
				switch (bl_brightness) {
				case KEYBOARD_BL_BRIGHTNESS_LOW:
					bl_brightness = KEYBOARD_BL_BRIGHTNESS_MED;
					break;
				case KEYBOARD_BL_BRIGHTNESS_MED:
					bl_brightness = KEYBOARD_BL_BRIGHTNESS_HIGH;
					break;
				case KEYBOARD_BL_BRIGHTNESS_HIGH:
					bl_brightness = KEYBOARD_BL_BRIGHTNESS_OFF;
					break;
				default:
				case KEYBOARD_BL_BRIGHTNESS_OFF:
					bl_brightness = KEYBOARD_BL_BRIGHTNESS_LOW;
					break;
				}
				kblight_set(bl_brightness);
			}
			/* we dont want to pass the space key event to the OS */
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
