/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Testing code. Running at Linux environment.
 */
#include "cros_ec/include/ec_common.h"
#include "cros_ec/include/core.h"
#include "cros_ec/chip_stub/keyboard.h"


int run_test_cases() {
  /* Just a simple test */
  SimulateKeyStateChange(2, 3, 1);
  /* Expect KeyboardStateChanged() in cros_ec/keyboard.c shows something. */

  return 0;
}


int main(int argc, char **argv) {

  EcError ret;

  /* Call Google EC core initial code. */
  ret = CoreMain();
  if (ret != EC_SUCCESS)
    return 1;

  return run_test_cases();
}
