# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Flash read/write/erase test
#

from flash_test_util import test_erase
from flash_test_util import test_read
from flash_test_util import test_write
import time

def test(helper):
    helper.wait_output("--- UART initialized")

    # Jump to RW-A
    helper.ec_command("sysjump a")
    helper.wait_output("idle task started") # jump completed
    time.sleep(0.5)

    # We are in RW now. Read/write/erase on RO should all succeed.
    test_erase(helper, 0, 0x1000)
    test_erase(helper, 0x1000, 0x2000)
    test_read(helper, 0, 0x40)
    test_read(helper, 0x130, 0x40)
    test_write(helper, 0, 0x30)
    test_write(helper, 0x130, 0x30)

    return True
