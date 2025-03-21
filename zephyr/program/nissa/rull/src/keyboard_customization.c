/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rull_keyboard, LOG_LEVEL_INF);

/*
 * check the key 30 (row:3, col:0), and 128 (row:6, col:15).
 */
static uint16_t kb_no_numpad_scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	/* KSO     KSI0    KSI1    KSI2    KSI3    KSI4    KSI5    KSI6  KSI7*/
	/*  0 */ { 0x0000, 0x0000, 0x0000, 0xe01f, 0x0000, 0x0000, 0x0000,
		   0x0000 },
	/*  1 */ { 0x0078, 0x0076, 0x000d, 0x000e, 0x001c, 0x0016, 0x001a, 0x003c },
	/*  2 */ { 0x0005, 0x000c, 0x0004, 0x0006, 0x0023, 0x0041, 0x0026, 0x0043 },
	/*  3 */ { 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x0049, 0x0025, 0x0044 },
	/*  4 */ { 0x0009, 0x0083, 0x000b, 0x001b, 0x0003, 0x004a, 0x001e, 0x004d },
	/*  5 */ { 0x0031, 0x0007, 0x005b, 0x000f, 0x0042, 0x0021, 0x003e, 0x0015 },
	/*  6 */ { 0x0051, 0x0033, 0x0035, 0x004e, 0x003b, 0x0029, 0x0045, 0x001d },
	/*  7 */ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059 },
	/*  8 */ { 0x0055, 0x0052, 0x0054, 0x0036, 0x004c, 0x0022, 0x003d, 0x0024 },
	/*  9 */ { 0xe06c, 0x0001, 0xe071, 0x002f, 0x004b, 0x002a, 0x0046, 0x002d },
	/* 10 */ { 0xe011, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	/* 11 */ { 0x0017, 0x0066, 0x000a, 0x005d, 0x005a, 0x003a, 0xe072, 0xe075 },
	/* 12 */ { 0x001f, 0x0064, 0xe07d, 0x0067, 0xe069, 0xe07a, 0xe074, 0xe06b },
	/* 13 */ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0011, 0x0000 },
	/* 14 */ { 0x0000, 0x0014, 0x0000, 0xe014, 0x0000, 0x0000, 0x0000, 0x0000 },
	/* 15 */ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0027, 0x0000 },
	/* 16 */ { 0x0037, 0x0037, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	/* 17 */ { 0x0000, 0x0000, 0x006a, 0x0000, 0x0000, 0x0000, 0x005d, 0x0061 },
};

/*
 * check the key 30 (row:3, col:0), and 128 (row:6, col:15).
 */
static uint16_t kb_numpad_scancode_set2[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	/* KSO   KSI0   KSI1    KSI2    KSI3    KSI4    KSI5    KSI6   KSI7*/
	/*  0 */ { 0x0000, 0x0000, 0x0000, 0xe01f, 0x0000, 0x0000, 0x0000,
		   0x0000 },
	/*  1 */ { 0xe01f, 0x0076, 0x000d, 0x000e, 0x001c, 0x0016, 0x001a, 0x003c },
	/*  2 */ { 0x0005, 0x000c, 0x0004, 0x0006, 0x0023, 0x0041, 0x0026, 0x0043 },
	/*  3 */ { 0x0032, 0x0034, 0x002c, 0x002e, 0x002b, 0x0049, 0x0025, 0x0044 },
	/*  4 */ { 0x0009, 0x0083, 0x000b, 0x001b, 0x0003, 0x004a, 0x001e, 0x004d },
	/*  5 */ { 0x0031, 0x0000, 0x005b, 0x0000, 0x0042, 0x0021, 0x003e, 0x0015 },
	/*  6 */ { 0x0051, 0x0033, 0x0035, 0x004e, 0x003b, 0x0029, 0x0045, 0x001d },
	/*  7 */ { 0x0000, 0x0000, 0x0061, 0x0000, 0x0000, 0x0012, 0x0000, 0x0059 },
	/*  8 */ { 0x0055, 0x0052, 0x0054, 0x0036, 0x004c, 0x0022, 0x003d, 0x0024 },
	/*  9 */ { 0xe06c, 0x0001, 0xe071, 0x002f, 0x004b, 0x002a, 0x0046, 0x002d },
	/* 10 */ { 0xe011, 0x0000, 0x006a, 0x0000, 0x0037, 0x0000, 0x005d, 0x0000 },
	/* 11 */ { 0xe071, 0x0066, 0x000a, 0x005d, 0x005a, 0x003a, 0xe072, 0xe075 },
	/* 12 */ { 0x0000, 0x0064, 0xe07d, 0x0067, 0xe069, 0xe07a, 0xe074, 0xe06b },
	/* 13 */ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0011, 0x0000 },
	/* 14 */ { 0x0000, 0x0014, 0x0000, 0xe014, 0x0000, 0x0000, 0x0000, 0x0000 },
	/* 15 */ { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0027, 0x0000 },
	/* 16 */ { 0xe04a, 0x007c, 0x007b, 0x0074, 0x0071, 0x0073, 0x006b, 0x0070 },
	/* 17 */ { 0x006c, 0x0075, 0x007d, 0x0079, 0x007a, 0x0072, 0x0069, 0xe05a },
};

/* Default set2 scan table without numpad designed */
static uint16_t (*scancode_set2)[KEYBOARD_ROWS] = kb_no_numpad_scancode_set2;

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

/*
 * Keyboard function decided by FW config.
 */
test_export_static void keyboard_matrix_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_NUMPAD, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d, "
			"assuming FW_KB_NUMERIC_PAD_PRESENT",
			FW_KB_NUMPAD);
		return;
	}

	switch (val) {
	case FW_KB_NUMPAD_PRESENT:
		scancode_set2 = kb_numpad_scancode_set2;
		LOG_INF("numpad keyboard matrix");
		break;
	case FW_KB_NUMPAD_NOT_PRESENT:
		scancode_set2 = kb_no_numpad_scancode_set2;
		LOG_INF("without_numpad keyboard matrix");
		break;
	default:
		LOG_WRN("invalid cbi value: %x", val);
		return;
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_matrix_init, HOOK_PRIO_POST_I2C);

#ifdef CONFIG_KEYBOARD_DEBUG
static uint8_t keycap_label[KEYBOARD_COLS_MAX][KEYBOARD_ROWS] = {
	{ 'c', KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ 'q', KLLI_UNKNO, KLLI_UNKNO, KLLI_TAB, '`', '1', KLLI_UNKNO, 'a' },
	{ KLLI_R_ALT, KLLI_L_ALT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ KLLI_UNKNO, KLLI_SPACE, 'e', KLLI_F4, KLLI_SEARC, '3', KLLI_F3,
	  KLLI_UNKNO },
	{ 'x', 'z', KLLI_F2, KLLI_F1, 's', '2', 'w', KLLI_ESC },
	{ 'v', 'b', 'g', 't', '5', '4', 'r', 'f' },
	{ 'm', 'n', 'h', 'y', '6', '7', 'u', 'j' },
	{ '.', KLLI_DOWN, '\\', 'o', KLLI_F10, '9', KLLI_UNKNO, 'l' },
	{ KLLI_R_SHT, KLLI_L_SHT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ ',', KLLI_UNKNO, KLLI_F7, KLLI_F6, KLLI_F5, '8', 'i', 'k' },
	{ KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO, KLLI_F9, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_LEFT, KLLI_UNKNO },
	{ KLLI_R_CTR, KLLI_L_CTR, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO },
	{ '/', KLLI_UP, '-', KLLI_UNKNO, '0', 'p', '[', ';' },
	{ '\'', KLLI_ENTER, KLLI_UNKNO, KLLI_UNKNO, '=', KLLI_B_SPC, ']', 'd' },
	{ KLLI_UNKNO, KLLI_F8, KLLI_RIGHT, KLLI_UNKNO, KLLI_UNKNO, KLLI_UNKNO,
	  KLLI_UNKNO, KLLI_UNKNO },
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
