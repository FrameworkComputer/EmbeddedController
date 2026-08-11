/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Sunflower input deck (IT8801 matrix keyboard controller).
 *
 * Implements input_deck abstraction: ops table at the end of the file.
 */

#include "common.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "driver/ioexpander/it8801.h"
#include "factory.h"
#include "gpio_signal.h"
#include "hid_device.h"
#include "hooks.h"
#include "host_command.h"
#include "input_deck.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_customization.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 80,
	.debounce_down_us = 20 * MSEC,
	.debounce_up_us = 20 * MSEC,
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

/* KSO mapping for discrete keyboard */
__override const uint8_t it8801_kso_mapping[] = {
	21, 20, 5, 19, 18, 17, 11, 14, 13, 15, 16, 12, 0, 2, 1, 3, 6, 4,
};
BUILD_ASSERT(ARRAY_SIZE(it8801_kso_mapping) == KEYBOARD_COLS_MAX);

/* ---------------------------------------------------------------------- */

/* KSI mapping: bitmask of rows to scan --> ksi mapping bitmask */
/* sunflower deck specific despite the naming. used by it8801 scan driver */
__override int it8801_ksi_mapping_transfer(int ksi_val)
{
	/* bits are only peeled off ksi_output, hence can be stored as u8 */
	uint8_t ksi_output = 255;

	if (ksi_val >= 255)
		return ksi_output;

	for (int i = 0; i < KEYBOARD_ROWS; i++) {
		static const uint8_t ksi_output_mask[] = {
			(uint8_t)~BIT(3), (uint8_t)~BIT(1), (uint8_t)~BIT(4),
			(uint8_t)~BIT(0), (uint8_t)~BIT(5), (uint8_t)~BIT(2)};

		if (!(ksi_val & BIT(i))) {
			if (i >= ARRAY_SIZE(ksi_output_mask))
				ksi_output &= ~BIT(i);
			else
				ksi_output &= ksi_output_mask[i];
		}
	}
	return (int)ksi_output;
}

/* ---------------------------------------------------------------------- */

/* This is a global symbol, as required by 8042 driver */
uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return scancode_set2[col][row];
	return 0;
}

/* ---------------------------------------------------------------------- */

/* Same as get_scancode_set2() above */
void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		scancode_set2[col][row] = val;
}

/* ---------------------------------------------------------------------- */

static const struct gpio_dt_spec sunflower_input_deck_tp_en =
	GPIO_DT_SPEC_GET(DT_NODELABEL(sunflower_deck), tp_en_gpios);

/* ---------------------------------------------------------------------- */

static const struct gpio_dt_spec sunflower_input_deck_lock_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(sunflower_deck), lock_led_gpios);

/* ---------------------------------------------------------------------- */

/* status bits for CAPS LED, any one bit set --> led off */
#define CAPS_LID_CLOSE		BIT(0)
#define CAPS_TABLET_MODE	BIT(1)
#define CAPS_SUSPEND		BIT(2)
#define CAPS_KEY_DISABLE	BIT(3)
#define CAPS_KEYBOARD_DISCONNECT BIT(4)

static uint8_t caps_led_off; /* can be treated as bool */

/* updates led status (blinks the led) */
static void caps_led_refresh(void)
{
	static bool pre_status; /* true, when led on */

	/* only toggle LED when change, pre_status is led light status last call */
	if ((bool)caps_led_off == pre_status) {
		pre_status = !pre_status;
		gpio_pin_set_dt(&sunflower_input_deck_lock_led, pre_status);
	}
}

/* ------------------------------------------------------------------------ */

/* Disable keyscan at boot */
/*
 * Until the deck is powered up and stable, don't let keyboard_scan poll
 * the (absent or unpowered) matrix
 */
static void input_deck_sunflower_disable_keyscan(void)
{
	keyboard_scan_enable(0, KB_SCAN_DISABLE_DISCONNECT);
}
DECLARE_HOOK(HOOK_INIT_EARLY, input_deck_sunflower_disable_keyscan, HOOK_PRIO_DEFAULT);

/* ------------------------------------------------------------------------ */

/* ---- Labels, for debugging ---- */
#ifdef CONFIG_PLATFORM_EC_KEYBOARD_DEBUG
static const char keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
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

/* ------------------------------------------------------------------------ */

uint8_t get_keycap_label(uint8_t row, uint8_t col)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		return keycap_label[col][row];
	return KLLI_UNKNO;
}

/* ------------------------------------------------------------------------ */

void set_keycap_label(uint8_t row, uint8_t col, uint8_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		keycap_label[col][row] = val;
}
#endif

/* ------------------------------------------------------------------------ */

/* States of the Fn key */
#define FN_PRESSED BIT(0)
#define FN_LOCKED  BIT(1)

static uint8_t fn_key; /* bit mask of FN_PRESSED | FN_LOCKED */
static uint32_t fn_key_table;
static uint32_t fn_key_table_media;

static int fn_table_media_set(int8_t pressed, uint32_t fn_bit)
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

/* ------------------------------------------------------------------------ */

static bool fn_table_set(int8_t pressed, uint32_t fn_bit)
{
	if (pressed && (fn_key & FN_PRESSED)) {
		fn_key_table |= fn_bit;
		return true;
	} else if (!pressed && (fn_key_table & fn_bit)) {
		fn_key_table &= ~fn_bit;
		return true;
	}
	return false;
}

/* ------------------------------------------------------------------------ */

/* save fn lock setting in bbram on power off, restored on power on */
static void input_deck_sunflower_save_settings(void)
{
	uint8_t current_kb = 0;

	if (fn_key & FN_LOCKED)
		current_kb |= 0x80;
	system_set_bbram(SYSTEM_BBRAM_IDX_KBSTATE, current_kb);

	fn_key &= ~FN_LOCKED;
	fn_key &= ~FN_PRESSED;
}

/* ------------------------------------------------------------------------ */

/* TODO: document/sync the BBRAM format with other input decks */
/* As it is now, one deck can save settings and another can load! */
static void input_deck_sunflower_load_settings(void)
{
	uint8_t current_kb = 0;

	if (system_get_bbram(SYSTEM_BBRAM_IDX_KBSTATE, &current_kb) == EC_SUCCESS) {
		if (current_kb & 0x80)
			fn_key |= FN_LOCKED;
	}
}

/* ------------------------------------------------------------------------ */

/* common hack to check if we are pre-OS */
static bool is_bios_mode(void)
{
	return !(*(host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS)) & BIT(0));
}

/* ------------------------------------------------------------------------ */

/* consumer control configuration, mapped to non-fn F12 (matches dahlia deck) */
#define HID_USAGEID_AL_CCC 0x0183u

/* media keys */
static int input_deck_sunflower_hotkey_F1_F12(uint16_t *key_code, uint16_t fn, int8_t pressed)
{
	const uint16_t prss_key = *key_code;

	if (!(fn_key & FN_LOCKED) && (fn & FN_PRESSED))
		return EC_SUCCESS;
	else if (fn_key & FN_LOCKED && !(fn & FN_PRESSED) && !fn_key_table_media)
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
		/* Print screen / SYSRQ */
		if (fn_table_media_set(pressed, KB_FN_F11))
			*key_code = 0xE07C;
		break;
	case SCANCODE_F12:  /* Framework logo key */
		if (fn_table_media_set(pressed, KB_FN_F12)) {
			hid_consumer(HID_USAGEID_AL_CCC, pressed);
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	default:
		return EC_SUCCESS;
	}
	return EC_SUCCESS;
}

/* ------------------------------------------------------------------------ */

/* letter special keys */
static int input_deck_sunflower_hotkey_special_key(uint16_t *key_code, int8_t pressed)
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

/* ------------------------------------------------------------------------ */

/* Fn+Esc/B/P/Space functional hotkeys */
static int input_deck_sunflower_functional_hotkey(uint16_t *key_code, int8_t pressed)
{
	const uint16_t prss_key = *key_code;

	switch (prss_key) {
	case SCANCODE_ESC: /* FUNCTION_LOCK */
		if (fn_table_set(pressed, KB_FN_ESC)) {
			if (pressed) {
				if (fn_key & FN_LOCKED)
					fn_key &= ~FN_LOCKED;
				else
					fn_key |= FN_LOCKED;
			}
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_B: /* BREAK_KEY */
		if (fn_table_set(pressed, KB_FN_B)) {
			if (pressed) {
				simulate_keyboard(0xe07e, 1);
				simulate_keyboard(0xe0, 1);
				simulate_keyboard(0x7e, 0);
			}
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	case SCANCODE_P: /* PAUSE_KEY */
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
	case SCANCODE_SPACE: /* backlight... there is none! */
		if (fn_table_set(pressed, KB_FN_SPACE)) {
			return EC_ERROR_UNIMPLEMENTED;
		}
		break;
	}
	return EC_SUCCESS;
}

/* ------------------------------------------------------------------------ */

static enum ec_error_list copilot_key(uint16_t *key_code, int8_t pressed)
{
	const uint16_t prss_key = *key_code;

	if (prss_key == SCANCODE_FAKE_COPILOT) {
		if (pressed) {
			simulate_keyboard(SCANCODE_LEFT_WIN, 1);
			simulate_keyboard(SCANCODE_LEFT_SHIFT, 1);
			simulate_keyboard(SCANCODE_F23, 1);
		} else {
			simulate_keyboard(SCANCODE_F23, 0);
			simulate_keyboard(SCANCODE_LEFT_SHIFT, 0);
			simulate_keyboard(SCANCODE_LEFT_WIN, 0);
		}
		/* Swallow the fake scancode itself. */
		*key_code = 0x0000;
	}
	return EC_SUCCESS;
}

/* ------------------------------------------------------------------------ */

enum ec_error_list keyboard_scancode_callback(uint16_t *make_code, int8_t pressed)
{
	const uint16_t pressed_key = *make_code;
	int r;

	if (factory_status())
		return EC_SUCCESS;

	if (pressed_key == SCANCODE_FN && pressed) {
		fn_key |= FN_PRESSED;
		return EC_ERROR_UNIMPLEMENTED;
	} else if (pressed_key == SCANCODE_FN && !pressed) {
		fn_key &= ~FN_PRESSED;
		return EC_ERROR_UNIMPLEMENTED;
	}

	/* If still in preOS, pass through everything unmodified. */
	if (is_bios_mode())
		return EC_SUCCESS;

	r = copilot_key(make_code, pressed);
	if (r != EC_SUCCESS)
		return r;

	r = input_deck_sunflower_hotkey_F1_F12(make_code, fn_key, pressed);
	if (r != EC_SUCCESS)
		return r;

	/* Without Fn held (or recently released), don't touch anything else. */
	if (!(fn_key & FN_PRESSED) && !fn_key_table)
		return EC_SUCCESS;

	r = input_deck_sunflower_hotkey_special_key(make_code, pressed);
	if (r != EC_SUCCESS)
		return r;

	r = input_deck_sunflower_functional_hotkey(make_code, pressed);
	if (r != EC_SUCCESS)
		return r;

	return EC_SUCCESS;
}

/* ------------------------------------------------------------------------ */

static bool input_deck_sunflower_keyscan_initialized;

static void input_deck_sunflower_power_on(void)
{
	input_deck_sunflower_load_settings();

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_3v_tp), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_tp), 1);

	input_deck_sunflower_keyscan_initialized = false;
}

/* ------------------------------------------------------------------------ */

static void input_deck_sunflower_power_off(void)
{
	if (input_deck_sunflower_keyscan_initialized)
		keyboard_scan_enable(0, KB_SCAN_DISABLE_DISCONNECT);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_tp), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_3v_tp), 0);

	input_deck_sunflower_save_settings();
}

/* ------------------------------------------------------------------------ */

static void input_deck_sunflower_enable_by_mode(void)
{
	bool enable = !tablet_get_mode() && lid_is_open();

	gpio_pin_set_dt(&sunflower_input_deck_tp_en, enable);
	/* start scan only if lid open and not tablet */
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_DISCONNECT);

	/* safe guard: close+open lid will reinit a disconnected deck */
	input_deck_sunflower_keyscan_initialized = enable;
}

/* ------------------------------------------------------------------------ */
static void input_deck_sunflower_resume(void)
{
	/* initialize keyscan, preferrably one */
	if (!input_deck_sunflower_keyscan_initialized) {
		int state;

		keyboard_scan_init();

		/*
		 * Now the keyboard init sequence will enable the key scan
		 * after crisis detection; it can't detect the Esc key press
		 * so let's detect Esc earlier.
		 */
		keyboard_raw_drive_column(KEYBOARD_COL_ESC);
		state = keyboard_raw_read_rows();
		keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
		check_bios_crisis_key(state & BIT(KEYBOARD_ROW_ESC));

		keyboard_raw_enable_interrupt(1);
		keyboard_raw_drive_column(KEYBOARD_COLUMN_ALL);
		input_deck_sunflower_keyscan_initialized = true;
	}

	/* enabled low, to clear it8801 driver cache */
	gpio_pin_configure_dt(&sunflower_input_deck_tp_en,    GPIO_OUTPUT_LOW);
	gpio_pin_configure_dt(&sunflower_input_deck_lock_led, GPIO_OUTPUT_LOW);

	/* enable keyscan / tp depending on tablet/clam/tent modes */
	input_deck_sunflower_enable_by_mode();

	/* per-deck pin + caps-LED setup */
	caps_led_off &= ~CAPS_SUSPEND;
	caps_led_off &= ~CAPS_KEYBOARD_DISCONNECT;
	caps_led_refresh();

	input_deck_sunflower_load_settings();
}

/* ------------------------------------------------------------------------ */

static void input_deck_sunflower_suspend(void)
{
	caps_led_off |= CAPS_SUSPEND;
	caps_led_off |= CAPS_KEYBOARD_DISCONNECT;
	caps_led_refresh();

	input_deck_sunflower_enable_by_mode();
}

/* ------------------------------------------------------------------------ */

static void input_deck_sunflower_kb_irq(void)
{
	io_expander_it8801_interrupt(GPIO_KB_DISCRETE_INT);
}

/* ------------------------------------------------------------------------ */

/* uses BIT(2) only */
static void input_deck_sunflower_caps_set_led(unsigned int data)
{
	/* 8042 uses BIT(2) for CAPS indicator */
	if (data & BIT(2))
		caps_led_off &= ~CAPS_KEY_DISABLE;
	else
		caps_led_off |= CAPS_KEY_DISABLE;
	caps_led_refresh();
}

/* ------------------------------------------------------------------------ */

static void input_deck_sunflower_lid_change(void)
{
	input_deck_sunflower_enable_by_mode();

	if (lid_is_open())
		caps_led_off &= ~CAPS_LID_CLOSE;
	else
		caps_led_off |= CAPS_LID_CLOSE;

	if (tablet_get_mode())
		caps_led_off |= CAPS_TABLET_MODE;
	else
		caps_led_off &= ~CAPS_TABLET_MODE;

	caps_led_refresh();
}

/* ------------------------------------------------------------------------ */

const struct input_deck input_deck_sunflower = {
	.power_on    = input_deck_sunflower_power_on,
	.power_off   = input_deck_sunflower_power_off,
	.resume      = input_deck_sunflower_resume,
	.suspend     = input_deck_sunflower_suspend,
	.kb_irq      = input_deck_sunflower_kb_irq,
	.set_kb_leds = input_deck_sunflower_caps_set_led,
	.lid_change  = input_deck_sunflower_lid_change,
};
