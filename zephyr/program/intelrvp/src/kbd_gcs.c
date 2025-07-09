/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "kbd_gcs.h"
#include "keyboard_8042_sharedlib.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kbd, LOG_LEVEL_INF);

/*
 * Keyboard-internal scancodes (Set 2)
 * https://kbd-project.org/docs/scancodes/scancodes-10.html
 */
enum gcs_scancodes {
	KNONE = 0x0000,

	KEY_ESC = 0x0076, /* Escape */
	/* Function keys are replaced by vivaldi key config in the dts file. */
	KEY_DEL = 0xe071, /* Delete */

	KEY_BKTK = 0x000e, /* Backtick */
	KEY_1 = SCANCODE_1,
	KEY_2 = SCANCODE_2,
	KEY_3 = SCANCODE_3,
	KEY_4 = SCANCODE_4,
	KEY_5 = SCANCODE_5,
	KEY_6 = SCANCODE_6,
	KEY_7 = SCANCODE_7,
	KEY_8 = SCANCODE_8,
	KEY_9 = 0x0046,
	KEY_0 = 0x0045,
	KEY_HPN = 0x004e, /* Hyphen */
	KEY_EQVL = 0x0055, /* Equal */
	KEY_BACKSP = 0x0066, /* Backspace */

	KEY_TAB = 0x000d,
	KEY_Q = 0x0015,
	KEY_W = 0x001d,
	KEY_E = 0x0024,
	KEY_R = 0x002d,
	KEY_T = 0x002c,
	KEY_Y = 0x0035,
	KEY_U = 0x003c,
	KEY_I = 0x0043,
	KEY_O = 0x0044,
	KEY_P = 0x004d,
	KEY_OBRC = 0x0054, /* Open brace */
	KEY_CBRC = 0x005b, /* Close brace */
	KEY_BSLSH = 0x005d, /* Backslash */

	KEY_KL = 0x0027, /* Caps lock */
	KEY_A = 0x001c,
	KEY_S = 0x001b,
	KEY_D = 0x0023,
	KEY_F = 0x002b,
	KEY_G = 0x0034,
	KEY_H = 0x0033,
	KEY_J = 0x003b,
	KEY_K = 0x0042,
	KEY_L = 0x004b,
	KEY_SCOL = 0x004c, /* Semicolon */
	KEY_APST = 0x0052, /* Apostrophe */
	KEY_ENTR = 0x005a, /* Enter */

	KEY_LSHFT = 0x0012, /* Left shift */
	KEY_Z = 0x001a,
	KEY_X = 0x0022,
	KEY_C = 0x0021,
	KEY_V = 0x002a,
	KEY_B = 0x0032,
	KEY_N = 0x0031,
	KEY_M = 0x003a,
	KEY_COM = 0x0041, /* Comma */
	KEY_DOT = 0x0049,
	KEY_FSLSH = 0x004a, /* Forwardslash */
	KEY_RSHFT = 0x0059, /* Right shift */

	KEY_LCTRL = 0x0014, /* Left control */
	KEY_FUNC = 0x0037, /* Function */
	KEY_GOOG = 0xe01f, /* Google assist */
	KEY_LALT = 0x0011, /* Left Alt */
	KEY_SPACE = 0x0029,
	KEY_RALT = 0xe011, /* Right Alt */
	KEY_RCTRL = 0xe014, /* Right control */
	KEY_LF = SCANCODE_LEFT, /* Left */
	KEY_UP = SCANCODE_UP, /* Up */
	KEY_DN = SCANCODE_DOWN, /* Down */
	KEY_RT = SCANCODE_RIGHT, /* Right */
};

/*
 * GCS keyboard scancodes
 */
static uint16_t scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ KNONE, KNONE, KNONE, KNONE, KNONE, KNONE, KEY_LCTRL, KNONE },
	{ KEY_Q, KEY_TAB, KEY_A, KEY_ESC, KEY_Z, KNONE, KEY_BKTK, KEY_1 },
	{ KEY_W, KEY_KL, KEY_S, KNONE, KEY_X, KNONE, KNONE, KEY_2 },
	{ KEY_E, KNONE, KEY_D, KNONE, KEY_C, KNONE, KNONE, KEY_3 },
	{ KEY_R, KEY_T, KEY_F, KEY_G, KEY_V, KEY_B, KEY_5, KEY_4 },
	{ KEY_U, KEY_Y, KEY_J, KEY_H, KEY_M, KEY_N, KEY_6, KEY_7 },
	{ KEY_I, KEY_CBRC, KEY_K, KNONE, KEY_COM, KNONE, KEY_EQVL, KEY_8 },
	{ KEY_O, KNONE, KEY_L, KNONE, KEY_DOT, KEY_RCTRL, KNONE, KEY_9 },
	{ KEY_P, KEY_OBRC, KEY_SCOL, KEY_APST, KEY_FUNC, KEY_FSLSH, KEY_HPN,
	  KEY_0 },
	{ KNONE, KNONE, KNONE, KEY_LALT, KNONE, KEY_RALT, KNONE, KNONE },
	{ KNONE, KEY_BACKSP, KEY_BSLSH, KNONE, KEY_ENTR, KNONE, KNONE, KNONE },
	{ KNONE, KNONE, KNONE, KEY_SPACE, KNONE, KEY_DN, KEY_DEL, KNONE },
	{ KNONE, KNONE, KNONE, KNONE, KNONE, KEY_RT, KNONE, KNONE },
	{ KNONE, KEY_GOOG, KNONE, KNONE, KNONE, KNONE, KNONE, KNONE },
	{ KNONE, KNONE, KNONE, KEY_UP, KNONE, KEY_LF, KNONE, KNONE },
	{ KNONE, KEY_LSHFT, KEY_RSHFT, KNONE, KNONE, KNONE, KNONE, KNONE },
};
BUILD_ASSERT(ARRAY_SIZE(scancode_set2) == KEYBOARD_COLS_MAX);

uint16_t get_scancode_set2(uint8_t row, uint8_t col)
{
	uint16_t ret_val = 0;

	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		ret_val = scancode_set2[col][row];

	LOG_DBG("Scancode get R%d:C%d=0x%x", row, col, ret_val);

	return ret_val;
}

void set_scancode_set2(uint8_t row, uint8_t col, uint16_t val)
{
	if (col < KEYBOARD_COLS_MAX && row < KEYBOARD_ROWS)
		scancode_set2[col][row] = val;

	LOG_DBG("Scancode set R%d:C%d=0x%x", row, col, val);
}
