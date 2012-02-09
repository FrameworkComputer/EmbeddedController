# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Task scheduling test
#

import time

# Test during 5s
DURATION=5

def test(helper):
      helper.wait_output("[starting Task T]")
      helper.wait_output("[starting Task C]")
      helper.wait_output("[starting Task B]")
      helper.wait_output("[starting Task A]")
      deadline = time.time() + DURATION
      count = []
      while time.time() < deadline:
          sched = helper.wait_output("(?P<a>(?:ABC){3,200})T", use_re=True,
                                     timeout=1)["a"]
          count.append(len(sched) / 3)

      helper.trace("IRQ count %d, cycles count min %d  max %d\n" %
                   (len(count), min(count), max(count)))
      return True # PASS !
