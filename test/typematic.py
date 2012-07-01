# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Keyboard typematic test
#

import time

KEY_PRESS_MSG = "i8042 SEND"

def expect_keypress(helper, lower_bound, upper_bound):
    for i in xrange(lower_bound + 1): # Plus 1 break code
        helper.wait_output(KEY_PRESS_MSG)
    for i in xrange(upper_bound - lower_bound):
        if helper.check_no_output(KEY_PRESS_MSG):
            return True
    if not helper.check_no_output(KEY_PRESS_MSG):
        return False
    return True

def test(helper):
    # Wait for EC initialized
    helper.wait_output("--- UART initialized")

    # Enable keyboard scanning
    helper.ec_command("kbd enable")
    time.sleep(0.1) # Workaround for crosbug/p/11015

    # Set typematic rate to 1000ms/500ms and hold down a key for 500ms
    # Expect 1 keypress.
    helper.ec_command("typematic 1000 500")
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(0.5)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 1, 1):
        return False

    # Hold down a key for 1200ms. Expect 2 keypress.
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(1.2)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 2, 2):
        return False

    # Hold down a key for 1700ms. Expect 3 keypress.
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(1.7)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 3, 3):
        return False

    # Hold down a key for 5400ms. Expect 9 or 10 keypress.
    # Due to inevitable delay incurred by each keypress, we cannot be certain
    # about the exact number of keypress. Therefore, mismatching by a small
    # amount should be accepted.
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(5.4)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 9, 10):
        return False

    return True # PASS !
