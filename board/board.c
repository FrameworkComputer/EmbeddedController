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

static uint16_t scancode_set2[CROS_ROW_NUM][CROS_COL_NUM] = {
  {0x0000, 0xe01f, 0x0005, 0x0032, 0x0009, 0x0000, 0x0031, 0x0000, 0x0055,
                                           0x0000, 0xe011, 0x0000, 0x0000},
  {0x0000, 0x0076, 0x000c, 0x0034, 0x0083, 0x0000, 0x0033, 0x0000, 0x0052,
                                           0x0001, 0x0000, 0x0066, 0x0000},
  {0x0014, 0x000d, 0x0004, 0x002c, 0x000b, 0x005b, 0x0035, 0x0000, 0x0054,
                                           0x000a, 0x0000, 0x0000, 0x0000},
  {0x0000, 0x000e, 0x0006, 0x002e, 0x0003, 0x0000, 0x0036, 0x0000, 0x004e,
                                           0x0000, 0x0000, 0x005d, 0x0000},
  {0xe014, 0x001c, 0x0023, 0x002b, 0x001b, 0x0042, 0x003b, 0x0000, 0x004c,
                                           0x004b, 0x0000, 0x005a, 0x0000},
  {0x0000, 0x001a, 0x0021, 0x002a, 0x0022, 0x0041, 0x003a, 0x0012, 0x004a,
                                           0x0049, 0x0000, 0x0029, 0x0000},
  {0x0000, 0x0016, 0x0026, 0x0025, 0x001e, 0x003e, 0x003d, 0x0000, 0x0045,
                                           0x0046, 0x0011, 0xe072, 0xe074},
  {0x0000, 0x0015, 0x0024, 0x002d, 0x001d, 0x0043, 0x003c, 0x0059, 0x004d,
                                           0x0044, 0x0000, 0xe075, 0xe06b},
};

static EcError MatrixCallback(
    int8_t row, int8_t col, int8_t pressed,
    enum EcScancodeSet code_set, uint8_t *scan_code, int32_t* len) {

  uint16_t make_code;

  EC_ASSERT(code_set == EC_SCANCODE_SET_2);  /* TODO: support other sets? */
  EC_ASSERT(scan_code);
  EC_ASSERT(len);

  if (row > CROS_ROW_NUM ||
      col > CROS_COL_NUM) {
    return EC_ERROR_INVALID_PARAMETER;
  }

  *len = 0;

  make_code = scancode_set2[row][col];
  EC_ASSERT(make_code);  /* there must be a make code mapping to a key */

  /* Output the make code (from table) */
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
