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
static enum EcScancodeSet scancode_set = EC_SCANCODE_SET_2;

static void KeyboardStateChanged(int row, int col, int is_pressed) {
  uint8_t scan_code[MAX_SCAN_CODE_LEN];
  int len;
  EcError ret;

  PRINTF("File %s:%s(): row=%d col=%d is_pressed=%d\n",
      __FILE__, __FUNCTION__, row, col, is_pressed);

  EC_ASSERT(matrix_callback);

  ret = matrix_callback(row, col, is_pressed, scancode_set, scan_code, &len);
  if (ret == EC_SUCCESS) {
    EC_ASSERT(len > 0);

    EcI8042SendScanCode(len, scan_code);
  } else {
    /* FIXME: long-term solution is to ignore this key. However, keep
     *        assertion in the debug stage. */
    EC_ASSERT(ret == EC_SUCCESS);
  }
}


static int HandleHostCommand(
    uint8_t command,
    uint8_t data,
    uint8_t *output) {
  int out_len = 0;

  switch (command) {
    case EC_I8042_CMD_GSCANSET:  /* also EC_I8042_CMD_SSCANSET */
      if (data == EC_SCANCODE_GET_SET) {
        output[out_len++] = scancode_set;
      } else if (data == EC_SCANCODE_SET_2) {
        scancode_set = data;
      } else {
        output[out_len++] = EC_I8042_RET_ERR;
      }
      break;

    case EC_I8042_CMD_SETREP:
    case EC_I8042_CMD_ENABLE:
    case EC_I8042_CMD_RESET_DIS:
    case EC_I8042_CMD_RESET_DEF:
    case EC_I8042_CMD_SETALL_MB:
    case EC_I8042_CMD_SETALL_MBR:
    case EC_I8042_CMD_RESET_BAT:
    case EC_I8042_CMD_RESEND:
    case EC_I8042_CMD_EX_ENABLE:
    case EC_I8042_CMD_EX_SETLEDS:
    case EC_I8042_CMD_OK_GETID:
    case EC_I8042_CMD_GETID:
    case EC_I8042_CMD_SETLEDS:
    default:
      output[out_len++] = EC_I8042_RET_ERR;
      break;
  }

  return out_len;
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

  ret = EcI8042RegisterCallback(HandleHostCommand);
  if (ret != EC_SUCCESS) return ret;

  return EC_SUCCESS;
}
