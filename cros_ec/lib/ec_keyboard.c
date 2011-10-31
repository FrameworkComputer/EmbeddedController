/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC keyboard code.
 */

#include "cros_ec/include/ec_common.h"
#include "cros_ec/include/ec_keyboard.h"
#include "chip_interface/ec_uart.h"
#include "chip_interface/keyboard.h"
#include "host_interface/i8042.h"


/*
 * i8042 global settings.
 */
static int i8042_enabled = 0;  /* default the keyboard is disabled. */
static uint8_t resend_command[MAX_I8042_OUTPUT_LEN];
static uint8_t resend_command_len = 0;

/*
 * Scancode settings
 */
static EcKeyboardMatrixCallback matrix_callback;
static enum EcScancodeSet scancode_set = EC_SCANCODE_SET_2;

/*
 * Typematic delay, rate and counter variables.
 *
 *    7     6     5     4     3     2     1     0
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |un-  |   delay   |     B     |        D        |
 * | used|  0     1  |  0     1  |  0     1     1  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * Formula:
 *   the inter-char delay = (2 ** B) * (D + 8) / 240 (sec)
 * Default: 500ms delay, 10.9 chars/sec.
 */
#define DEFAULT_TYPEMATIC_VALUE ((1 << 5) || (1 << 3) || (3 << 0))
#define DEFAULT_FIRST_DELAY 500
#define DEFAULT_INTER_DELAY 91
static uint8_t typematic_value_from_host = DEFAULT_TYPEMATIC_VALUE;
static int refill_first_delay = DEFAULT_FIRST_DELAY;  /* unit: ms */
static int counter_first_delay;
static int refill_inter_delay = DEFAULT_INTER_DELAY;  /* unit: ms */
static int counter_inter_delay;


static void reset_rate_and_delay() {
  typematic_value_from_host = DEFAULT_TYPEMATIC_VALUE;
  refill_first_delay = DEFAULT_FIRST_DELAY;
  refill_inter_delay = DEFAULT_INTER_DELAY;
}


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
  int save_for_resend = 1;
  int i;

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

    case EC_I8042_CMD_SETLEDS:  /* fall-thru */
    case EC_I8042_CMD_EX_SETLEDS:
      /* We use screen indicator. Do thing in keyboard controller. */
      output[out_len++] = EC_I8042_RET_ACK;
      break;

    case EC_I8042_CMD_GETID:    /* fall-thru */
    case EC_I8042_CMD_OK_GETID:
      output[out_len++] =  0xab;  /* Regular keyboards */
      output[out_len++] =  0x83;
      break;

    case EC_I8042_CMD_SETREP:
      typematic_value_from_host = data;
      refill_first_delay = counter_first_delay + counter_inter_delay;
      refill_first_delay = ((typematic_value_from_host & 0x60) >> 5) * 250;
      refill_inter_delay = 1000 *  /* ms */
                           (1 << ((typematic_value_from_host & 0x18) >> 3)) *
                           ((typematic_value_from_host & 0x7) + 8) /
                           240;
      break;

    case EC_I8042_CMD_ENABLE:
      i8042_enabled = 1;
      /* TODO: clean the underlying internal buffer */
      break;

    case EC_I8042_CMD_RESET_DIS:
      i8042_enabled = 0;
      reset_rate_and_delay();
      /* TODO: clean the underlying internal buffer */
      break;

    case EC_I8042_CMD_RESET_DEF:
      reset_rate_and_delay();
      /* TODO: clean the underlying internal buffer */
      break;

    case EC_I8042_CMD_RESET_BAT:
      i8042_enabled = 0;
      output[out_len++] = EC_I8042_RET_BAT;
      output[out_len++] = EC_I8042_RET_BAT;
      /* TODO: clean the underlying internal buffer */
      break;

    case EC_I8042_CMD_RESEND:
      save_for_resend = 0;
      for (i = 0; i < resend_command_len; ++i) {
        output[out_len++] = resend_command[i];
      }
      break;

    case EC_I8042_CMD_SETALL_MB:  /* fall-thru below */
    case EC_I8042_CMD_SETALL_MBR:
    case EC_I8042_CMD_EX_ENABLE:
    default:
      output[out_len++] = EC_I8042_RET_ERR;
      EcUartPrintf("Unsupported i8042 command 0x%02x.\n", command);
      break;
  }

  /* For resend, keep output before leaving. */
  if (out_len && save_for_resend) {
    EC_ASSERT(out_len <= MAX_I8042_OUTPUT_LEN);
    for (i = 0; i < out_len; ++i) {
      resend_command[i] = output[i];
    }
    resend_command_len = out_len;
  }

  EC_ASSERT(out_len <= MAX_I8042_OUTPUT_LEN);
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
