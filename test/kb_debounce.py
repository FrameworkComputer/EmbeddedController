# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Keyboard debounce test
#

import time

SHORTER_THAN_DEBOUNCE_TIME = 0.005 # 5ms
LONGER_THAN_DEBOUNCE_TIME = 0.020 # 20ms
KEYPRESS_REGEX = "\[KB state: (?P<km>[0-9\s-]*)\]"

def consume_output(helper, reg_ex):
    done = False
    while not done:
        try:
            helper.wait_output(reg_ex, use_re=True, timeout=1)
        except:
            done = True

def get_key_count(s):
    key_count_map = {'1': 1, '2': 1, '3': 2, '4': 1, '5': 2, '6': 2, '7': 3,
                     '8': 1, '9': 2, 'a': 2, 'b': 3, 'c': 2, 'd': 3, 'e': 3,
                     'e': 4, '-': 0, ' ': 0, '0': 0}
    return reduce(lambda x, y: x + key_count_map[y], s, 0)

def expect_key_count(helper, cnt):
    s = helper.wait_output(KEYPRESS_REGEX, use_re=True, timeout=1)["km"]
    act_cnt = get_key_count(s)
    if act_cnt != cnt:
        helper.trace("Expecting %d key press, got %d." % (cnt, act_cnt))
        return False
    else:
        return True

def test(helper):
      # Wait for EC initialized
      helper.wait_output("--- UART initialized")

      # Enable keyboard scanning and disable typematic
      helper.ec_command("kbd enable")
      helper.ec_command("typematic 1000000 1000000")

      # Press for a short period and check this is ignored
      consume_output(helper, KEYPRESS_REGEX)
      helper.ec_command("mockmatrix 1 1 1")
      time.sleep(SHORTER_THAN_DEBOUNCE_TIME)
      helper.ec_command("mockmatrix 1 1 0")
      if not helper.check_no_output(KEYPRESS_REGEX, use_re=True):
          return False

      # Press for a longer period and check keypress is accepted
      consume_output(helper, KEYPRESS_REGEX)
      helper.ec_command("mockmatrix 1 1 1")
      time.sleep(LONGER_THAN_DEBOUNCE_TIME)
      helper.ec_command("mockmatrix 1 1 0")
      if not expect_key_count(helper, 1): # Press
          return False
      if not expect_key_count(helper, 0): # Release
          return False

      # Press and release for a short period, and then press for a longer
      # period and check exactly one keypress is accepted
      consume_output(helper, KEYPRESS_REGEX)
      helper.ec_command("mockmatrix 1 1 1")
      time.sleep(SHORTER_THAN_DEBOUNCE_TIME)
      helper.ec_command("mockmatrix 1 1 0")
      time.sleep(SHORTER_THAN_DEBOUNCE_TIME)
      helper.ec_command("mockmatrix 1 1 1")
      time.sleep(LONGER_THAN_DEBOUNCE_TIME)
      helper.ec_command("mockmatrix 1 1 0")
      if not expect_key_count(helper, 1): # Press
          return False
      if not expect_key_count(helper, 0): # Release
          return False
      if not helper.check_no_output(KEYPRESS_REGEX, use_re=True):
          return False

      # Hold down a key and press another key for a short period. Expect
      # this event is ignored
      consume_output(helper, KEYPRESS_REGEX)
      helper.ec_command("mockmatrix 1 1 1")
      if not expect_key_count(helper, 1):
          return False
      helper.ec_command("mockmatrix 2 2 1")
      time.sleep(SHORTER_THAN_DEBOUNCE_TIME)
      helper.ec_command("mockmatrix 2 2 0")
      if not helper.check_no_output(KEYPRESS_REGEX, use_re=True):
          return False
      helper.ec_command("mockmatrix 1 1 0")
      if not expect_key_count(helper, 0):
          return False

      return True # Pass!
