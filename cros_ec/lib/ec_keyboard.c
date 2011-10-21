/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC keyboard code.
 */

#include "cros_ec/include/ec_common.h"
#include "chip_interface/keyboard.h"

static void KeyboardStateChanged(int col, int row, int is_pressed) {
  PRINTF("File %s:%s(): col=%d row=%d is_pressed=%d\n",
      __FILE__, __FUNCTION__, col, row, is_pressed);
}


EcError EcKeyboardInit() {
  EcError ret;

  ret = EcKeyboardRegisterCallback(KeyboardStateChanged);
  if (ret != EC_SUCCESS) return ret;

  return EC_SUCCESS;
}
