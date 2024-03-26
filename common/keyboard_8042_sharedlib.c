/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Objects which can be shared between RO and RW for 8042 keyboard protocol.
 */

#include "builtin/assert.h"
#include "button.h"
#include "console.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "libsharedobjs.h"
#include "util.h"

#include <stddef.h>

#ifndef CONFIG_KEYBOARD_CUSTOMIZATION
/* The standard Chrome OS keyboard matrix table in scan code set 2. */
static scancode_set2_t scancode_set2_default = {
	{ 0x0000, 0x0000, 0x0014, 0xe01f, 0xe014, 0xe007, 0x0000, 0x0000 },
	{ 0xe01f, 0x0076, 0x000d, 0x000e, 0x001c, 0x001a, 0x0016, 0x0015 },
	{ 0x0005, 0x000c, 0x0004, 0x0006, 0x0023, 0x0021, 0x0026, 0x0024 },
	{ 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x002a, 0x0025, 0x002d },
	{ 0x0009, 0x0083, 0x000b, 0x0003, 0x001b, 0x0022, 0x001e, 0x001d },
	{ 0x0051, 0x0000, 0x005b, 0x0000, 0x0042, 0x0041, 0x003e, 0x0043 },
	{ 0x0031, 0x0033, 0x0035, 0x0036, 0x003b, 0x003a, 0x003d, 0x003c },
	{ 0x0000, 0x0000, 0x0061, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059 },
	{ 0x0055, 0x0052, 0x0054, 0x004e, 0x004c, 0x004a, 0x0045, 0x004d },
	{ 0x0000, 0x0001, 0x000a, 0x002f, 0x004b, 0x0049, 0x0046, 0x0044 },
	{ 0xe011, 0x0000, 0x006a, 0x0000, 0x005d, 0x0000, 0x0011, 0x0000 },
#ifndef CONFIG_KEYBOARD_KEYPAD
	{ 0x0000, 0x0066, 0x0000, 0x005d, 0x005a, 0x0029, 0xe072, 0xe075 },
	{ 0x0000, 0x0064, 0x0000, 0x0067, 0x0000, 0x0000, 0xe074, 0xe06b },
#else
	{ 0x0000, 0x0066, 0xe071, 0x005d, 0x005a, 0x0029, 0xe072, 0xe075 },
	{ 0xe06c, 0x0064, 0xe07d, 0x0067, 0xe069, 0xe07a, 0xe074, 0xe06b },
	{ 0xe04a, 0x007c, 0x007b, 0x0074, 0x0071, 0x0073, 0x006b, 0x0070 },
	{ 0x006c, 0x0075, 0x007d, 0x0079, 0x007a, 0x0072, 0x0069, 0xe05a },
#endif
};
BUILD_ASSERT(ARRAY_SIZE(scancode_set2_default) == KEYBOARD_COLS);

test_export_static scancode_set2_t *scancode_set2 = &scancode_set2_default;

void register_scancode_set2(scancode_set2_t *scancode_set, uint8_t cols)
{
	cprintf(CC_KEYBOARD, "%s: 0x%p -> %p (cols:%d->%d)\n", __func__,
		scancode_set2, scancode_set, keyboard_get_cols(), cols);
	keyboard_set_cols(cols);
	scancode_set2 = scancode_set;
}

uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	if (col < keyboard_get_cols() && row < KEYBOARD_ROWS)
		return (*scancode_set2)[col][row];
	return 0;
}

void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < keyboard_get_cols() && row < KEYBOARD_ROWS)
		(*scancode_set2)[col][row] = val;
}

#endif /* CONFIG_KEYBOARD_CUSTOMIZATION */

/*
 * The translation table from scan code set 2 to set 1.
 * Ref: http://kbd-project.org/docs/scancodes/scancodes-10.html#ss10.3
 * To reduce space, we only keep the translation for 0~127,
 * so a real translation need to do 0x83=>0x41 explicitly (
 * see scancode_translate_set2_to_1 below).
 */
SHAREDLIB(const uint8_t scancode_translate_table[128] = {
		  0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58, 0x64, 0x44,
		  0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59, 0x65, 0x38, 0x2a, 0x70,
		  0x1d, 0x10, 0x02, 0x5a, 0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11,
		  0x03, 0x5b, 0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
		  0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d, 0x69, 0x31,
		  0x30, 0x23, 0x22, 0x15, 0x07, 0x5e, 0x6a, 0x72, 0x32, 0x24,
		  0x16, 0x08, 0x09, 0x5f, 0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b,
		  0x0a, 0x60, 0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
		  0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e, 0x3a, 0x36,
		  0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76, 0x55, 0x56, 0x77, 0x78,
		  0x79, 0x7a, 0x0e, 0x7b, 0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e,
		  0x7f, 0x6f, 0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
		  0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
	  });

#ifdef CONFIG_KEYBOARD_DEBUG
SHAREDLIB(
	const static char *const
		keycap_long_label[KLLI_MAX & KEYCAP_LONG_LABEL_INDEX_BITMASK] = {
			"UNKNOWN", "F1",    "F2",    "F3",    "F4",    "F5",
			"F6",	   "F7",    "F8",    "F9",    "F10",   "F11",
			"F12",	   "F13",   "F14",   "F15",   "L-ALT", "R-ALT",
			"L-CTR",   "R-CTR", "L-SHT", "R-SHT", "ENTER", "SPACE",
			"B-SPC",   "TAB",   "SEARC", "LEFT",  "RIGHT", "DOWN",
			"UP",	   "ESC",
		});

const char *get_keycap_long_label(uint8_t idx)
{
	if (idx < ARRAY_SIZE(keycap_long_label))
		return keycap_long_label[idx];
	return "UNKNOWN";
}

#ifndef CONFIG_KEYBOARD_CUSTOMIZATION
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_L_CTR, KLLI_SEARC, KLLI_R_CTR,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_F11, KLLI_ESC, KLLI_TAB, '~', 'a', 'z', '1', 'q' },
	{ KLLI_F1, KLLI_F4, KLLI_F3, KLLI_F2, 'd', 'c', '3', 'e' },
	{ 'b', 'g', 't', '5', 'f', 'v', '4', 'r' },
	{ KLLI_F10, KLLI_F7, KLLI_F6, KLLI_F5, 's', 'x', '2', 'w' },
	{ KLLI_UNKNO, KLLI_F12, ']', KLLI_F13, 'k', ',', '8', 'i' },
	{ 'n', 'h', 'y', '6', 'j', 'm', '7', 'u' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_L_SHT, KLLI_UNKNO, KLLI_R_SHT },
	{ '=', '\'', '[', '-', ';', '/', '0', 'p' },
	{ KLLI_F14, KLLI_F9, KLLI_F8, KLLI_UNKNO, '|', '.', '9', 'o' },
	{ KLLI_R_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_L_ALT, KLLI_UNKNO },
	{ KLLI_F15, KLLI_B_SPC, KLLI_UNKNO, '\\', KLLI_ENTER, KLLI_SPACE,
	  KLLI_DOWN, KLLI_UP },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_RIGHT, KLLI_LEFT },
#ifdef CONFIG_KEYBOARD_KEYPAD
	/* TODO: Populate these */
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
#endif
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
#endif /* CONFIG_KEYBOARD_CUSTOMIZATION */
#endif /* CONFIG_KEYBOARD_DEBUG */

uint8_t scancode_translate_set2_to_1(uint8_t code)
{
	if (code & 0x80) {
		if (code == 0x83)
			return 0x41;
		return code;
	}
	return scancode_translate_table[code];
}

/*
 * Button scan codes.
 * Must be in the same order as defined in keyboard_button_type.
 */
SHAREDLIB(const struct button_8042_t buttons_8042[] = {
		  { SCANCODE_POWER, 0 },
		  { SCANCODE_VOLUME_DOWN, 1 },
		  { SCANCODE_VOLUME_UP, 1 },
		  { SCANCODE_1, 1 },
		  { SCANCODE_2, 1 },
		  { SCANCODE_3, 1 },
		  { SCANCODE_4, 1 },
		  { SCANCODE_5, 1 },
		  { SCANCODE_6, 1 },
		  { SCANCODE_7, 1 },
		  { SCANCODE_8, 1 },
	  });
BUILD_ASSERT(ARRAY_SIZE(buttons_8042) == KEYBOARD_BUTTON_COUNT);
