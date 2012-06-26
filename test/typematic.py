# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Keyboard typematic test
#

import time

KEY_PRESS_MSG = "i8042 SEND"

def check_no_output(helper, reg_ex):
    success = False
    try:
        helper.wait_output(reg_ex, use_re=True, timeout=1)
    except:
        success = True
    return success

def expect_keypress(helper, cnt):
    for i in xrange(cnt + 1): # Plus 1 break code
        helper.wait_output(KEY_PRESS_MSG)
    if not check_no_output(helper, KEY_PRESS_MSG):
        return False
    return True

def test(helper):
    # Wait for EC initialized
    helper.wait_output("--- UART initialized")

    # Enable keyboard scanning
    helper.ec_command("kbd enable")

    # Set typematic rate to 1000ms/500ms and hold down a key for 500ms
    # Expect 1 keypress.
    helper.ec_command("typematic 1000 500")
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(0.5)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 1):
        return False

    # Hold down a key for 1200ms. Expect 2 keypress.
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(1.2)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 2):
        return False

    # Hold down a key for 1700ms. Expect 3 keypress.
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(1.7)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 3):
        return False

    # Hold down a key for 5400ms. Expect 10 keypress.
    # Here we choose 5400ms to allow short delay when each keypress is sent.
    # If we choose time length too close to 5000ms, we might end up getting
    # only 9 keypress.
    helper.ec_command("mockmatrix 1 1 1")
    time.sleep(5.4)
    helper.ec_command("mockmatrix 1 1 0")
    if not expect_keypress(helper, 10):
        return False

    return True # PASS !
