/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The functions implemented by keyboard component of EC core.
 */

#ifndef __CROS_INCLUDE_EC_KEYBOARD_H
#define __CROS_INCLUDE_EC_KEYBOARD_H

#include "cros_ec/include/ec_common.h"

/* The initialize code of keyboard lib. Called by core main. */
EcError EcKeyboardInit();


/* Register the board-specific keyboard matrix translation function.
 * The callback function accepts col/row and returns the scan code.
 *
 * Note that *scan_code must be at least 4 bytes long to store maximum
 * possible sequence.
 */
typedef EcError (*EcKeyboardMatrixCallback)(
    int8_t row, int8_t col, int8_t pressed,
    uint8_t *scan_code, int32_t* len);

EcError EcKeyboardMatrixRegisterCallback(
    int8_t row_num, int8_t col_num,
    EcKeyboardMatrixCallback callback);

#define MAX_SCAN_CODE_LEN 4


#endif  /* __CROS_INCLUDE_EC_KEYBOARD_H */
