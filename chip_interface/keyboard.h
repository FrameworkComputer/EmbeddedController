/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * keyboard.h - Keyboard interface between EC core and EC Lib.
 */

#ifndef __CHIP_INTERFACE_KEYBOARD_H
#define __CHIP_INTERFACE_KEYBOARD_H

#include "cros_ec/include/ec_common.h"

#define MAX_KEYBOARD_MATRIX_COLS 16
#define MAX_KEYBOARD_MATRIX_ROWS 8

typedef void (*EcKeyboardCallback)(int col, int row, int is_pressed);

/* Registers a callback function to underlayer EC lib. So that any key state
 * change would notify the upper EC main code.
 *
 * Note that passing NULL removes any previously registered callback.
 */
EcError EcKeyboardRegisterCallback(EcKeyboardCallback cb);

/* Asks the underlayer EC lib what keys are pressed right now.
 *
 * Sets bit_array to a debounced array of which keys are currently pressed,
 * where a 1-bit means the key is pressed. For example, if only col=2 row=3
 * is pressed, it would set bit_array to {0, 0, 0x08, 0, ...}
 *
 * bit_array must be at least MAX_KEYBOARD_MATRIX_COLS bytes long.
 */
EcError EcKeyboardGetState(uint8_t *bit_array);

#endif  /* __CHIP_INTERFACE_KEYBOARD_H */
