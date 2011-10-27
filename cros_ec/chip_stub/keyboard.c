/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chip stub code of keyboard. Implements the chip interface.
 */

#include <stdint.h>
#include "chip_interface/keyboard.h"

static EcKeyboardCallback core_keyboard_callback;
static uint8_t virtual_matrix[MAX_KEYBOARD_MATRIX_COLS];

EcError EcKeyboardRegisterCallback(EcKeyboardCallback cb) {
  core_keyboard_callback = cb;
  return EC_SUCCESS;
}


EcError EcKeyboardGetState(uint8_t *bit_array) {
  /* TODO: implement later */
  return EC_SUCCESS;
}


/* Called by test code. This simulates a key press or release.
 * Usually, the test code would expect a scan code is received at host side.
 */
EcError SimulateKeyStateChange(int row, int col, int state) {
  EC_ASSERT(row < MAX_KEYBOARD_MATRIX_ROWS);
  EC_ASSERT(col < MAX_KEYBOARD_MATRIX_COLS);

  if (!core_keyboard_callback) return EC_ERROR_UNKNOWN;

  state = (state) ? 1 : 0;
  int current_state = (virtual_matrix[col] >> row) & 1;

  if (state && !current_state) {
    /* key is just pressed down */
    virtual_matrix[col] |= 1 << row;
    core_keyboard_callback(row, col, state);
  } else if (!state && current_state) {
    virtual_matrix[col] &= ~(1 << row);
    core_keyboard_callback(row, col, state);
  } else {
    /* Nothing happens if a key has been pressed or released. */
  }

  return EC_SUCCESS;
}
