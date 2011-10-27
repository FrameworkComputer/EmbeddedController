/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC keyboard code.
 */

#include "cros_ec/include/ec_common.h"
#include "cros_ec/include/ec_keyboard.h"
#include "chip_interface/keyboard.h"
#include "host_interface/i8042.h"


static EcKeyboardMatrixCallback matrix_callback;

static void KeyboardStateChanged(int row, int col, int is_pressed) {
  uint8_t scan_code[MAX_SCAN_CODE_LEN];
  int len;
  EcError ret;

  PRINTF("File %s:%s(): row=%d col=%d is_pressed=%d\n",
      __FILE__, __FUNCTION__, row, col, is_pressed);

  EC_ASSERT(matrix_callback);

  ret = matrix_callback(row, col, is_pressed, scan_code, &len);
  if (ret == EC_SUCCESS) {
    EC_ASSERT(len > 0);

    EcI8042SendScanCode(len, scan_code);
  } else {
    /* FIXME: long-term solution is to ignore this key. However, keep
     *        assertion in the debug stage. */
    EC_ASSERT(ret == EC_SUCCESS);
  }
}


EcError EcKeyboardMatrixRegisterCallback(
    int8_t row_num, int8_t col_num,
    EcKeyboardMatrixCallback callback) {

  matrix_callback = callback;

  return EC_SUCCESS;
}


EcError EcKeyboardInit() {
  EcError ret;

  ret = EcKeyboardRegisterCallback(KeyboardStateChanged);
  if (ret != EC_SUCCESS) return ret;

  return EC_SUCCESS;
}
