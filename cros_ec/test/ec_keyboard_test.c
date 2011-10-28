/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Testing code. Running at Linux environment.
 */
#include "cros_ec/include/ec_common.h"
#include "chip_interface/ec_uart.h"
#include "cros_ec/include/core.h"
#include "cros_ec/include/ec_keyboard.h"
#include "cros_ec/chip_stub/include/host.h"
#include "cros_ec/chip_stub/include/keyboard.h"
#include "host_interface/i8042.h"

#define RUN_TEST(func) do {  \
      int ret;  \
      ret = func();  \
      if (ret != EC_SUCCESS) {  \
        EcUartPrintf("Test %s() failed, retval = %d\n", #func, ret);  \
        return ret;  \
      }  \
    } while (0)


EcError TestKeyMade() {
  uint8_t buf;

  /* Fake a key press */
  SimulateKeyStateChange(2, 3, 1);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0x24);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  /* The duplicate press event will be ignored. */
  SimulateKeyStateChange(2, 3, 1);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  /* Test 2-byte scan code */
  SimulateKeyStateChange(3, 6, 1);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0xE0);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0x1f);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  return EC_SUCCESS;
}

EcError TestKeyReleased() {
  uint8_t buf;

  /* The key is not pressed yet. A release event doesn't send out a code. */
  SimulateKeyStateChange(0, 1, 0);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  /* Press and release it. Expect a release code. */
  SimulateKeyStateChange(0, 1, 1);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0x0E);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));
  SimulateKeyStateChange(0, 1, 0);  /* release */
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0xF0);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0x0E);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  /* Test 3-byte break code */
  SimulateKeyStateChange(7, 3, 1);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0xE0);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0x37);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));
  SimulateKeyStateChange(7, 3, 0);  /* release */
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0xE0);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0xF0);
  EC_ASSERT(EC_SUCCESS == PullI8042ScanCode(&buf));
  EC_ASSERT(buf == 0x37);
  EC_ASSERT(EC_ERROR_BUFFER_EMPTY == PullI8042ScanCode(&buf));

  return EC_SUCCESS;
}


EcError TestScancodeSet() {
  int len;
  uint8_t output[MAX_I8042_OUTPUT_LEN];

  /* Get Scancode Set */
  len = SimulateI8042Command(EC_I8042_CMD_GSCANSET, EC_SCANCODE_GET_SET,
                             output);
  EC_ASSERT(len == 1);
  EC_ASSERT(output[0] == EC_SCANCODE_SET_2);

  /* Set as set 1. Expect failed */
  len = SimulateI8042Command(EC_I8042_CMD_GSCANSET, EC_SCANCODE_SET_1, output);
  EC_ASSERT(len == 1);
  EC_ASSERT(output[0] == EC_I8042_RET_ERR);

  /* Set as set 2. Expect success */
  len = SimulateI8042Command(EC_I8042_CMD_GSCANSET, EC_SCANCODE_SET_2, output);
  EC_ASSERT(len == 0);

  /* Set as set 3. Expect failed */
  len = SimulateI8042Command(EC_I8042_CMD_GSCANSET, EC_SCANCODE_SET_3, output);
  EC_ASSERT(len == 1);
  EC_ASSERT(output[0] == EC_I8042_RET_ERR);

  return EC_SUCCESS;
}

int run_test_cases() {
  RUN_TEST(TestKeyMade);
  RUN_TEST(TestKeyReleased);
  RUN_TEST(TestScancodeSet);

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
