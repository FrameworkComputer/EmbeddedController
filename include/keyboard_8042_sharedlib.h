/* Copyright 2015 The Chromium OS Authors. All rights reserved.
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

struct button_8042_t {
	uint16_t scancode;
	int repeat;
};

/* The standard Chrome OS keyboard matrix table. */
#ifdef CONFIG_KEYBOARD_SCANCODE_MUTABLE
extern uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];
#else
extern const uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];
#endif

/* Translation from scan code set 2 to set 1. */
extern const uint8_t scancode_translate_table[];
extern uint8_t scancode_translate_set2_to_1(uint8_t code);

#ifdef CONFIG_KEYBOARD_DEBUG
#define KEYCAP_LONG_LABEL_BIT   (0x80)
#define KEYCAP_LONG_LABEL_INDEX_BITMASK		(~KEYCAP_LONG_LABEL_BIT)

enum keycap_long_label_idx {
	KLLI_UNKNO = 0x80,		/* UNKNOWN */
	KLLI_F1    = 0x81,		/* F1 or PREVIOUS */
	KLLI_F2    = 0x82,		/* F2 or NEXT */
	KLLI_F3    = 0x83,		/* F3 or REFRESH */
	KLLI_F4    = 0x84,		/* F4 or FULL_SCREEN */
	KLLI_F5    = 0x85,		/* F5 or OVERVIEW */
	KLLI_F6    = 0x86,		/* F6 or DIM */
	KLLI_F7    = 0x87,		/* F7 or BRIGHT */
	KLLI_F8    = 0x88,		/* F8 or MUTE */
	KLLI_F9    = 0x89,		/* F9 or VOLUME DOWN */
	KLLI_F10   = 0x8A,		/* F10 or VOLUME UP */
	KLLI_F11   = 0x8B,		/* F11 or POWER */
	KLLI_F12   = 0x8C,		/* F12 or DEV TOOLS */
	KLLI_F13   = 0x8D,		/* F13 or GOOGLE ASSISTANT */
	KLLI_RSVD4 = 0x8E,
	KLLI_RSVD5 = 0x8F,
	KLLI_L_ALT = 0x90,		/* LEFT ALT */
	KLLI_R_ALT = 0x91,		/* RIGHT ALT */
	KLLI_L_CTR = 0x92,		/* LEFT CONTROL */
	KLLI_R_CTR = 0x93,		/* RIGHT CONTROL */
	KLLI_L_SHT = 0x94,		/* LEFT SHIFT */
	KLLI_R_SHT = 0x95,		/* RIGHT SHIFT */
	KLLI_ENTER = 0x96,		/* ENTER */
	KLLI_SPACE = 0x97,		/* SPACE */
	KLLI_B_SPC = 0x98,		/* BACk SPACE*/
	KLLI_TAB   = 0x99,		/* TAB */
	KLLI_SEARC = 0x9A,		/* SEARCH */
	KLLI_LEFT  = 0x9B,		/* LEFT ARROW */
	KLLI_RIGHT = 0x9C,		/* RIGHT ARROW */
	KLLI_DOWN  = 0x9D,		/* DOWN ARROW */
	KLLI_UP    = 0x9E,		/* UP ARROW */
	KLLI_ESC   = 0x9F,		/* ESCAPE */
	KLLI_MAX
};

extern const char * const keycap_long_label[];
#ifdef CONFIG_KEYBOARD_SCANCODE_MUTABLE
extern char keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];
#else
extern const char keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];
#endif
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

	SCANCODE_F1 = 0x0005,
	SCANCODE_F2 = 0x0006,
	SCANCODE_F3 = 0x0004,
	SCANCODE_F4 = 0x000c,
	SCANCODE_F5 = 0x0003,
	SCANCODE_F6 = 0x000b,
	SCANCODE_F7 = 0x0083,
	SCANCODE_F8 = 0x000a,

	SCANCODE_UP = 0xe075,
	SCANCODE_DOWN = 0xe072,
	SCANCODE_LEFT = 0xe06b,
	SCANCODE_RIGHT = 0xe074,

	SCANCODE_LEFT_CTRL = 0x0014,
	SCANCODE_RIGHT_CTRL = 0xe014,
	SCANCODE_LEFT_ALT = 0x0011,
	SCANCODE_RIGHT_ALT = 0xe011,

	SCANCODE_LEFT_WIN = 0xe01f,  /* Also known as GUI or Super key. */
	SCANCODE_RIGHT_WIN = 0xe027,
	SCANCODE_MENU = 0xe02f,

	SCANCODE_POWER = 0xe037,
	SCANCODE_VOLUME_DOWN = 0xe021,
	SCANCODE_VOLUME_UP = 0xe032,

	SCANCODE_NUMLOCK = 0x0077,
	SCANCODE_CAPSLOCK = 0x0058,
	SCANCODE_SCROLL_LOCK = 0x007e,

	SCANCODE_CTRL_BREAK = 0xe07e,
};

#endif /* __CROS_EC_KEYBOARD_8042_SHAREDLIB_H */
