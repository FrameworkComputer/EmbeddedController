/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * main.c -- the main function of board-specific code.
 */

#include "cros_ec/include/ec_common.h"
#include "board/board_interface.h"
#include "cros_ec/include/ec_keyboard.h"

#define CROS_ROW_NUM 8  /* TODO: +1 for power button. */
#define CROS_COL_NUM 13

/* The standard Chrome OS keyboard matrix table. */
static uint8_t maxtri_table[CROS_ROW_NUM][CROS_COL_NUM] = {
  /* FIXME: waiting for approval to open-source this table. */
};

static uint16_t scancode_set2[128] = {
  /*   0 */ 0x0000, 0x000E, 0x0016, 0x001E, 0x0026, 0x0025, 0x002E, 0x0036,
  /*   8 */ 0x003d, 0x003e, 0x0046, 0x0045, 0x004E, 0x0055, 0x0000, 0x0066,
  /*  16 */ 0x000d, 0x0015, 0x001d, 0x0024, 0x002d, 0x002c, 0x0035, 0x003c,
  /*  24 */ 0x0043, 0x0044, 0x004D, 0x0054, 0x005b, 0x005d, 0xE01F, 0x001c,
  /*  32 */ 0x001b, 0x0023, 0x002b, 0x0034, 0x0033, 0x003B, 0x0042, 0x004B,
  /*  40 */ 0x004C, 0x0052, 0x0000, 0x005A, 0x0012, 0x0000, 0x001A, 0x0022,
  /*  48 */ 0x0021, 0x002A, 0x0032, 0x0031, 0x003A, 0x0041, 0x0049, 0x004A,
  /*  56 */ 0x0000, 0x0059, 0x0014, 0xE037, 0x0011, 0x0029, 0xE011, 0x0000,
  /*  64 */ 0xE014, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  /*  72 */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xe06B,
  /*  80 */ 0x0000, 0x0000, 0x0000, 0xE075, 0xE072, 0x0000, 0x0000, 0x0000,
  /*  88 */ 0x0000, 0xE074, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  /*  96 */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  /* 104 */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0076, 0x0000,
  /* 112 */ 0x0005, 0x0006, 0x0004, 0x000c, 0x0003, 0x000b, 0x0083, 0x0009,
  /* 120 */ 0x0000, 0x000a, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static EcError MatrixCallback(
    int8_t row, int8_t col, int8_t pressed,
    enum EcScancodeSet code_set, uint8_t *scan_code, int32_t* len) {

  int key_index;  /* the index og scancode_set */
  uint16_t make_code;

  EC_ASSERT(code_set == EC_SCANCODE_SET_2);  /* TODO: support other sets? */
  EC_ASSERT(scan_code);
  EC_ASSERT(len);

  if (row > CROS_ROW_NUM ||
      col > CROS_COL_NUM) {
    return EC_ERROR_INVALID_PARAMETER;
  }

  *len = 0;

#if 1  /* FIXME: remove #if after we can opensource the matrix table. */
  /* fake */
  maxtri_table[0][0] = scancode_set2[0];  // FIXME: to make compiler happy
  make_code = scancode_set2[key_index = row * 8 + col];
#else
  key_index = maxtri_table[row][col];
  EC_ASSERT(key_index < (sizeof(scancode_set2) / sizeof(scancode_set2[0])));
  make_code = scancode_set2[key_index];
#endif

  /* Output the make code (from table) */
  EC_ASSERT(make_code);  /* there must be a make code mapping to a key */
  if (make_code >= 0x0100) {
    *len += 2;
    scan_code[0] = make_code >> 8;
    scan_code[1] = make_code & 0xff;
  } else {
    *len += 1;
    scan_code[0] = make_code & 0xff;
  }

  /* insert the break byte, move back the last byte and insert a 0xf0 byte
   * before that. */
  if (!pressed) {
    EC_ASSERT(*len >= 1);
    scan_code[*len] = scan_code[*len - 1];
    scan_code[*len - 1] = 0xF0;
    *len += 1;
  }

  return EC_SUCCESS;
}


EcError BoardInit() {
  EC_ASSERT(EC_SUCCESS ==
      EcKeyboardMatrixRegisterCallback(
          CROS_ROW_NUM, CROS_COL_NUM, MatrixCallback));

  return EC_SUCCESS;
}
