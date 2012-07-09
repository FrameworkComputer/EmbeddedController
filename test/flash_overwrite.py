# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Flash overwrite test
#

from flash_test_util import get_flash_size
from flash_test_util import get_ro_size
from flash_test_util import test_write
import time

def test(helper):
    helper.wait_output("--- UART initialized")

    # Jump to RO
    helper.ec_command("sysjump ro")
    helper.wait_output("idle task started") # jump completed
    time.sleep(0.5)

    # Get flash info
    flashsize = get_flash_size(helper)
    rosize = get_ro_size(helper)

    # We are in RO now. Writing to RO should fail.
    test_write(helper, rosize / 2, 0x30, expect_fail=True)

    # Writing to RW should succeed.
    test_write(helper, rosize, 0x30) # begin of RW
    test_write(helper, (rosize + flashsize) / 2, 0x30) # mid-point of RW
    test_write(helper, flashsize - 0x30, 0x30) # end of flash

    # Jump to RW-A
    helper.ec_command("sysjump a")
    helper.wait_output("idle task started") # jump completed
    time.sleep(0.5)

    # We are in RW now. Writing to RO should succeed.
    test_write(helper, 0, 0x30) # begin of RO
    test_write(helper, rosize / 2, 0x30) # mid-point of RO
    test_write(helper, rosize - 0x30, 0x30) # end of RO

    # Writing to RW-A should fail.
    test_write(helper, rosize, 0x30, expect_fail=True)

    return True
