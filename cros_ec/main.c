/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This is the main function of Chrome OS EC core.
 * Called by platform-dependent main function.
 *
 */
#include "cros_ec/include/core.h"
#include "cros_ec/include/ec_common.h"
#include "cros_ec/include/ec_keyboard.h"
#include "board/board_interface.h"


#define ReturnIfInitFailed(func) \
    do {  \
      EcError ret;  \
      ret = func();  \
      if (ret != EC_SUCCESS) {  \
        printf("%s() failed at %s:%d: %d\n", #func, __FILE__, __LINE__, ret);  \
        return ret;  \
      }  \
    } while (0)


EcError CoreMain() {

  ReturnIfInitFailed(EcKeyboardInit);
  ReturnIfInitFailed(BoardInit);

  return EC_SUCCESS;
}
