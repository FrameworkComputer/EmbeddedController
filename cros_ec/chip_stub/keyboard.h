/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The keyboard stub header file for test code to include.
 */


#ifndef __EC_CHIP_STUB_KEYBOARD_H_
#define __EC_CHIP_STUB_KEYBOARD_H_

EcError SimulateKeyStateChange(int col, int row, int state);

#endif  /* __EC_CHIP_STUB_KEYBOARD_H_ */
