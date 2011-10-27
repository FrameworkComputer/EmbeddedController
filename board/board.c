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

static EcError MatrixCallback(
    int8_t row, int8_t col, int8_t pressed,
    uint8_t *scan_code, int32_t* len) {

  uint8_t make_code;

  EC_ASSERT(scan_code);
  EC_ASSERT(len);

  if (row > CROS_ROW_NUM ||
      col > CROS_COL_NUM) {
    return EC_ERROR_INVALID_PARAMETER;
  }

  make_code = maxtri_table[row][col];
  make_code = row * 13 + col;  /* FIXME: remove this after we can open-source
                                * the matrix table. */

  if (pressed) {
    *scan_code = make_code;
    *len = 1;
  } else {
    *scan_code = make_code | 0x80;
    *len = 1;
  }

  return EC_SUCCESS;
}


EcError BoardInit() {
  EC_ASSERT(EC_SUCCESS ==
      EcKeyboardMatrixRegisterCallback(
          CROS_ROW_NUM, CROS_COL_NUM, MatrixCallback));

  return EC_SUCCESS;
}
