# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Timers test
#

import time

# Test during 5s
DURATION=5

# Linear congruential pseudo random number generator*/
def prng(x):
    return (22695477 * x + 1) & 0xffffffff

# period between 500us and 128ms
def period_us(num):
    return (((num % 256) + 1) * 500)

# build the same pseudo random sequence as the target
def build_sequence():
    #TODO
    return []

def test(helper):
      helper.wait_output("[Timer task ")
      deadline = time.time() + DURATION
      seq = []
      while time.time() < deadline:
          tmr = helper.wait_output("(?P<t>[0-9])", use_re=True,
                                     timeout=1)["t"]
          seq.append(tmr)

      # Check the results
      model = build_sequence()
      #TODO

      helper.trace("Got %d timer IRQ\n" % len(seq))

      return True # PASS !
