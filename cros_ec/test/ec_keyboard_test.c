/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Testing code. Running at Linux environment.
 */
#include "cros_ec/include/ec_common.h"
#include "chip_interface/ec_uart.h"
#include "cros_ec/include/core.h"
#include "cros_ec/chip_stub/include/host.h"
#include "cros_ec/chip_stub/include/keyboard.h"

#define RUN_TEST(func) do {  \
      int ret;  \
      ret = func();  \
      if (ret != EC_SUCCESS) {  \
        EcUartPrintf("Test %s() failed, retval = %d\n", #func, ret);  \
        return ret;  \
      }  \
    } while (0)


EcError testKeyMade() {
  uint8_t buf;

  SimulateKeyStateChange(2, 3, 1);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 2 * 13 + 3);

  /* The duplicate press event will be ignored. */
  SimulateKeyStateChange(2, 3, 1);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  return EC_SUCCESS;
}

EcError testKeyReleased() {
  uint8_t buf;

  /* The key is not pressed yet. A release event doesn't send out a code. */
  SimulateKeyStateChange(7, 12, 0);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  /* Press and release it. Expect a release code. */
  SimulateKeyStateChange(7, 12, 1);
  SimulateKeyStateChange(7, 12, 0);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 7 * 13 + 12);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 7 * 13 + 12 + 0x80);

  return EC_SUCCESS;
}


int run_test_cases() {
  RUN_TEST(testKeyMade);
  RUN_TEST(testKeyReleased);

  return EC_SUCCESS;
}


int main(int argc, char **argv) {
  EcError ret;

  /* Call Google EC core initial code. */
  ret = CoreMain();
  if (ret != EC_SUCCESS)
    return 1;

  return run_test_cases();
}
