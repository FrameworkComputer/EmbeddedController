/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "keyboard_customization.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_backlight.h"
#include "pwm.h"

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

// void board_keyboard_drive_col(int col)
// {
// 	/* Drive all lines to high */
// 	if (col == KEYBOARD_COLUMN_NONE)
// 		gpio_set_level(GPIO_KBD_KSO4, 0);

// 	/* Set KBSOUT to zero to detect key-press */
// 	else if (col == KEYBOARD_COLUMN_ALL)
// 		gpio_set_level(GPIO_KBD_KSO4, 1);

// 	/* Drive one line for detection */
// 	else {
// 		if (col == 4)
// 			gpio_set_level(GPIO_KBD_KSO4, 1);
// 		else
// 			gpio_set_level(GPIO_KBD_KSO4, 0);
// 	}
// }


#ifdef CONFIG_KEYBOARD_DEBUG
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
#ifdef CONFIG_KEYBOARD_KEYPAD
	/* TODO: Populate these */
	{KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
			KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO},
	{KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
			KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO},
#endif
};

char get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return keycap_label[col][row];
	return KLLI_UNKNO;
}

void set_keycap_label(uint8_t row, uint8_t col, char val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		keycap_label[col][row] = val;
}
#endif


#ifdef CONFIG_KEYBOARD_BACKLIGHT
static uint8_t backlight_state;
enum backlight_brightness {
	KEYBOARD_BL_BRIGHTNESS_OFF = 0,
	KEYBOARD_BL_BRIGHTNESS_LOW = 20,
	KEYBOARD_BL_BRIGHTNESS_MED = 50,
	KEYBOARD_BL_BRIGHTNESS_HIGH = 100,
};

static int hx20_kblight_enable(int enable)
{
	if (board_get_version() > 4)
		pwm_set_duty(PWM_CH_KBL, 0);

	return EC_SUCCESS;
}

int hx20_kblight_disable(void)
{
	if (board_get_version() > 4)
		pwm_set_duty(PWM_CH_KBL, 0);

	backlight_state = KEYBOARD_BL_BRIGHTNESS_OFF;

	return EC_SUCCESS;
}

static int hx20_kblight_set_brightness(int percent)
{
	if (board_get_version() > 4)
		pwm_set_duty(PWM_CH_KBL, percent);
	else
		gpio_set_level(GPIO_EC_KBL_PWR_EN, percent ? 1 : 0);

	return EC_SUCCESS;
}

static int hx20_kblight_get_brightness(void)
{
	if (board_get_version() > 4)
		return pwm_get_duty(PWM_CH_KBL);
	else
		return gpio_get_level(GPIO_EC_KBL_PWR_EN) ? 100 : 0;

}

static int hx20_kblight_init(void)
{
	if (board_get_version() > 4)
		pwm_set_duty(PWM_CH_KBL, 0);
	else
		gpio_set_level(GPIO_EC_KBL_PWR_EN, 0);

	return EC_SUCCESS;
}

const struct kblight_drv kblight_hx20 = {
	.init = hx20_kblight_init,
	.set = hx20_kblight_set_brightness,
	.get = hx20_kblight_get_brightness,
	.enable = hx20_kblight_enable,
};

void board_kblight_init(void)
{
	kblight_register(&kblight_hx20);
}
#endif

#ifdef CONFIG_KEYBOARD_CUSTOMIZATION_COMBINATION_KEY
#define FN_PRESSED BIT(0)
#define FN_LOCKED BIT(1)
static uint8_t Fn_key;

enum ec_error_list keyboard_scancode_callback(uint16_t *make_code,
					      int8_t pressed)
{
	const uint16_t pressed_key = *make_code;


	if (pressed_key == SCANCODE_FN && pressed)
		Fn_key |= FN_PRESSED;
	else if (pressed_key == SCANCODE_FN && !pressed)
		Fn_key &= ~FN_PRESSED;

	if (!pressed)
		return EC_SUCCESS;

	/*
	*If the function key is not held, then
	* we pass through all events without modifying them
	*/
	if (Fn_key == 0)
		return EC_SUCCESS;

	switch (pressed_key) {
	case SCANCODE_ESC: /* TODO: FUNCTION_LOCK */
		break;
	case SCANCODE_F1:  /* SPEAKER_MUTE */
		*make_code = SCANCODE_VOLUME_MUTE;
		break;
	case SCANCODE_F2:  /* VOLUME_DOWN */
		*make_code = SCANCODE_VOLUME_DOWN;

		break;
	case SCANCODE_F3:  /* VOLUME_UP */
		*make_code = SCANCODE_VOLUME_UP;

		break;
	case SCANCODE_F4:  /* TODO: MIC_MUTE */

		break;
	case SCANCODE_F5:  /* PLAY_PAUSE */
		*make_code = SCANCODE_PLAY_PAUSE;

		break;
	case SCANCODE_F6:  /* DIM_SCREEN */
		*make_code = SCANCODE_BRIGHTNESS_DOWN;

		break;
	case SCANCODE_F7:  /* BRIGHTEN_SCREEN */
		*make_code = SCANCODE_BRIGHTNESS_UP;

		break;
	case SCANCODE_F8:  /* TODO: EXTERNAL_DISPLAY */

		break;
	case SCANCODE_F9:  /* TODO: TOGGLE_WIFI */

		break;
	case SCANCODE_F10:  /* TODO: TOGGLE_BLUETOOTH */

		break;
	case SCANCODE_F11:
			/* *
			* TODO this might need an
			* extra key combo of:
			* 0xE012 0xE07C to simulate
			* PRINT_SCREEN
			*/
		*make_code = 0xE07C;
		break;
	case SCANCODE_F12:  /* TODO: FRAMEWORK */
		*make_code = 0xE02F;

		break;
	case SCANCODE_DELETE:  /* TODO: INSERT */
		*make_code = 0xE070;
		break;
	case SCANCODE_B:
			/* *
			* TODO this might need an
			* extra key combo of: E1 14 77 E1 F0 14 F0 77
			* TODO: BREAK_KEY
			*/
		break;
	case SCANCODE_K:  /* TODO: SCROLL_LOCK */
		*make_code = 0x7E;
		break;
	case SCANCODE_P:  /* TODO: PAUSE */
		break;
	case SCANCODE_S:  /* TODO: SYSRQ */

		break;
	case SCANCODE_SPACE:	/* TODO: TOGGLE_KEYBOARD_BACKLIGHT */
		switch (backlight_state) {
		case KEYBOARD_BL_BRIGHTNESS_LOW:
			backlight_state = KEYBOARD_BL_BRIGHTNESS_MED;
			break;
		case KEYBOARD_BL_BRIGHTNESS_MED:
			backlight_state = KEYBOARD_BL_BRIGHTNESS_HIGH;
			break;
		case KEYBOARD_BL_BRIGHTNESS_HIGH:
			backlight_state = KEYBOARD_BL_BRIGHTNESS_OFF;
			break;
		default:
		case KEYBOARD_BL_BRIGHTNESS_OFF:
			backlight_state = KEYBOARD_BL_BRIGHTNESS_LOW;
			break;
		}
		kblight_set(backlight_state);
		/* we dont want to pass the space key event to the OS */
		return EC_ERROR_UNKNOWN;
		break;
	case SCANCODE_LEFT:  /* HOME */
		*make_code = 0xE06C;
		break;
	case SCANCODE_RIGHT:  /* END */
		*make_code = 0xE069;
		break;
	case SCANCODE_UP:  /* PAGE_UP */
		*make_code = 0xE07D;
		break;
	case SCANCODE_DOWN:  /* PAGE_DOWN */
		*make_code = 0xE07A;
		break;
	}
	return EC_SUCCESS;
}
#endif