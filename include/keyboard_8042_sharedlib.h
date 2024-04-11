/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __CROS_EC_KEYBOARD_8042_SHAREDLIB_H
#define __CROS_EC_KEYBOARD_8042_SHAREDLIB_H

#include "button.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"

#include <stddef.h>

struct button_8042_t {
	uint16_t scancode;
	int repeat;
};

typedef uint16_t scancode_set2_t[][KEYBOARD_ROWS];

/**
 * Register scancode set for the standard ChromeOS keyboard matrix set 2.
 *
 * @param scancode_set	Scancode set to register.
 * @param cols Scancode column size
 */
void register_scancode_set2(scancode_set2_t *scancode_set, uint8_t cols);

/**
 * Get the standard Chrome OS keyboard matrix set 2 scanset
 * @param row	Row number
 * @param col	Column number
 * @return	0 on error, scanset for the (row,col) if successful
 **/
uint16_t get_scancode_set2(uint8_t row, uint8_t col);
/**
 * Set the standard Chrome OS keyboard matrix set 2 scanset
 * @param row	Row number
 * @param col	Column number
 * @param val	Value to set
 **/
void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val);

/* Translation from scan code set 2 to set 1. */
extern const uint8_t scancode_translate_table[];
extern uint8_t scancode_translate_set2_to_1(uint8_t code);

#ifdef CONFIG_KEYBOARD_DEBUG
#define KEYCAP_LONG_LABEL_BIT (0x80)
#define KEYCAP_LONG_LABEL_INDEX_BITMASK (~KEYCAP_LONG_LABEL_BIT)

enum keycap_long_label_idx {
	KLLI_UNKNO = 0x80, /* UNKNOWN */
	KLLI_F1 = 0x81, /* F1 or PREVIOUS */
	KLLI_F2 = 0x82, /* F2 or NEXT */
	KLLI_F3 = 0x83, /* F3 or REFRESH */
	KLLI_F4 = 0x84, /* F4 or FULL_SCREEN */
	KLLI_F5 = 0x85, /* F5 or OVERVIEW */
	KLLI_F6 = 0x86, /* F6 or DIM */
	KLLI_F7 = 0x87, /* F7 or BRIGHT */
	KLLI_F8 = 0x88, /* F8 or MUTE */
	KLLI_F9 = 0x89, /* F9 or VOLUME DOWN */
	KLLI_F10 = 0x8A, /* F10 or VOLUME UP */
	KLLI_F11 = 0x8B, /* F11 or POWER */
	KLLI_F12 = 0x8C, /* F12 or DEV TOOLS */
	KLLI_F13 = 0x8D, /* F13 or GOOGLE ASSISTANT */
	KLLI_F14 = 0x8E, /* F14 */
	KLLI_F15 = 0x8F, /* F15 */
	KLLI_L_ALT = 0x90, /* LEFT ALT */
	KLLI_R_ALT = 0x91, /* RIGHT ALT */
	KLLI_L_CTR = 0x92, /* LEFT CONTROL */
	KLLI_R_CTR = 0x93, /* RIGHT CONTROL */
	KLLI_L_SHT = 0x94, /* LEFT SHIFT */
	KLLI_R_SHT = 0x95, /* RIGHT SHIFT */
	KLLI_ENTER = 0x96, /* ENTER */
	KLLI_SPACE = 0x97, /* SPACE */
	KLLI_B_SPC = 0x98, /* BACk SPACE*/
	KLLI_TAB = 0x99, /* TAB */
	KLLI_SEARC = 0x9A, /* SEARCH */
	KLLI_LEFT = 0x9B, /* LEFT ARROW */
	KLLI_RIGHT = 0x9C, /* RIGHT ARROW */
	KLLI_DOWN = 0x9D, /* DOWN ARROW */
	KLLI_UP = 0x9E, /* UP ARROW */
	KLLI_ESC = 0x9F, /* ESCAPE */
	KLLI_MAX
};

/**
 * Get the keycap "long version" label
 * @param idx	Index into keycap_long_label_idx[]
 * @return	"UNKNOWN" on error, long label for idx if successful
 */
const char *get_keycap_long_label(uint8_t idx);

/**
 * Get the keycap label
 * @param row	Row number
 * @param col	Column number
 * @return	KLLI_UNKNO on error, label for the (row,col) if successful
 */
uint8_t get_keycap_label(uint8_t row, uint8_t col);
/**
 * Set the keycap label
 * @param row	Row number
 * @param col	Column number
 * @param val	Value to set
 */
void set_keycap_label(uint8_t row, uint8_t col, uint8_t val);
#endif

/* Button scancodes (Power, Volume Down, Volume Up, etc.) */
extern const struct button_8042_t buttons_8042[KEYBOARD_BUTTON_COUNT];

/* Scan code set 2 table. */
enum scancode_values {
	SCANCODE_1 = 0x0016,
	SCANCODE_2 = 0x001e,
	SCANCODE_3 = 0x0026,
	SCANCODE_4 = 0x0025,
	SCANCODE_5 = 0x002e,
	SCANCODE_6 = 0x0036,
	SCANCODE_7 = 0x003d,
	SCANCODE_8 = 0x003e,

	SCANCODE_A = 0x001c,
	SCANCODE_B = 0x0032,
	SCANCODE_T = 0x002c,

	SCANCODE_F1 = 0x0005, /* Translates to 3b in codeset 1 */
	SCANCODE_F2 = 0x0006, /* Translates to 3c in codeset 1 */
	SCANCODE_F3 = 0x0004, /* Translates to 3d in codeset 1 */
	SCANCODE_F4 = 0x000c, /* Translates to 3e in codeset 1 */
	SCANCODE_F5 = 0x0003, /* Translates to 3f in codeset 1 */
	SCANCODE_F6 = 0x000b, /* Translates to 40 in codeset 1 */
	SCANCODE_F7 = 0x0083, /* Translates to 41 in codeset 1 */
	SCANCODE_F8 = 0x000a, /* Translates to 42 in codeset 1 */
	SCANCODE_F9 = 0x0001, /* Translates to 43 in codeset 1 */
	SCANCODE_F10 = 0x0009, /* Translates to 44 in codeset 1 */
	SCANCODE_F11 = 0x0078, /* Translates to 57 in codeset 1 */
	SCANCODE_F12 = 0x0007, /* Translates to 58 in codeset 1 */
	SCANCODE_F13 = 0x000f, /* Translates to 59 in codeset 1 */
	SCANCODE_F14 = 0x0017, /* Translates to 5a in codeset 1 */
	SCANCODE_F15 = 0x001f, /* Translates to 5b in codeset 1 */

	SCANCODE_BACK = 0xe038, /* e06a in codeset 1 */
	SCANCODE_REFRESH = 0xe020, /* e067 in codeset 1 */
	SCANCODE_FORWARD = 0xe030, /* e069 in codeset 1 */
	SCANCODE_FULLSCREEN = 0xe01d, /* e011 in codeset 1 */
	SCANCODE_OVERVIEW = 0xe024, /* e012 in codeset 1 */
	SCANCODE_SNAPSHOT = 0xe02d, /* e013 in codeset 1 */
	SCANCODE_BRIGHTNESS_DOWN = 0xe02c, /* e014 in codeset 1 */
	SCANCODE_BRIGHTNESS_UP = 0xe035, /* e015 in codeset 1 */
	SCANCODE_PRIVACY_SCRN_TOGGLE = 0xe03c, /* e016 in codeset 1 */
	SCANCODE_VOLUME_MUTE = 0xe023, /* e020 in codeset 1 */
	SCANCODE_VOLUME_DOWN = 0xe021, /* e02e in codeset 1 */
	SCANCODE_VOLUME_UP = 0xe032, /* e030 in codeset 1 */
	SCANCODE_KBD_BKLIGHT_DOWN = 0xe043, /* e017 in codeset 1 */
	SCANCODE_KBD_BKLIGHT_UP = 0xe044, /* e018 in codeset 1 */
	SCANCODE_KBD_BKLIGHT_TOGGLE = 0xe01c, /* e01e in codeset 1 */
	SCANCODE_NEXT_TRACK = 0xe04d, /* e019 in codeset 1 */
	SCANCODE_PREV_TRACK = 0xe015, /* e010 in codeset 1 */
	SCANCODE_PLAY_PAUSE = 0xe054, /* e01a in codeset 1 */
	SCANCODE_MICMUTE = 0xe05b, /* e01b in codeset 1 */
	SCANCODE_DICTATE = 0xe04c, /* e027 in codeset 1 */

	SCANCODE_UP = 0xe075,
	SCANCODE_DOWN = 0xe072,
	SCANCODE_LEFT = 0xe06b,
	SCANCODE_RIGHT = 0xe074,

	SCANCODE_LEFT_CTRL = 0x0014,
	SCANCODE_RIGHT_CTRL = 0xe014,
	SCANCODE_LEFT_ALT = 0x0011,
	SCANCODE_RIGHT_ALT = 0xe011,

	SCANCODE_LEFT_WIN = 0xe01f, /* Also known as GUI or Super key. */
	SCANCODE_RIGHT_WIN = 0xe027,
	SCANCODE_MENU = 0xe02f,

	SCANCODE_POWER = 0xe037,

	SCANCODE_NUMLOCK = 0x0077,
	SCANCODE_CAPSLOCK = 0x0058,
	SCANCODE_SCROLL_LOCK = 0x007e,

	SCANCODE_CTRL_BREAK = 0xe07e,
};

#endif /* __CROS_EC_KEYBOARD_8042_SHAREDLIB_H */
