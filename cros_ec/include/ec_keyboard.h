/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard emulation */

#ifndef __CROS_EC_KEYBOARD_H
#define __CROS_EC_KEYBOARD_H


/* The initialize code of keyboard lib. Called by core main. */
EcError EcKeyboardInit();


/* Register the board-specific keyboard matrix translation function.
 * The callback function accepts col/row and returns the scan code.
 */
EcError EcKeyboardMatrixRegister(
    int8_t col_num, int8_t row_num,
    EcError (*callback)(
        int8_t column, int8_t row, int8_t pressed,
        uint8_t *scan_code, int32_t* len));


#endif  /* __CROS_EC_KEYBOARD_H */
