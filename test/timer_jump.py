# Copyright 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Timer test: check time consistency when jumping between images
#

import time

DELAY = 5
ERROR_MARGIN = 0.5

def test(helper):
      helper.wait_output("idle task started")
      helper.ec_command("sysinfo")
      copy = helper.wait_output("Copy:\s+(?P<c>\S+)", use_re=True)["c"]
      if copy != "RO":
          helper.ec_command("sysjump ro")
          helper.wait_output("idle task started")
      helper.ec_command("gettime")
      ec_start_time = helper.wait_output("Time: 0x[0-9a-f]* = (?P<t>[\d\.]+) s",
                                         use_re=True)["t"]
      time.sleep(DELAY)
      helper.ec_command("sysjump a")
      helper.wait_output("idle task started")
      helper.ec_command("gettime")
      ec_end_time = helper.wait_output("Time: 0x[0-9a-f]* = (?P<t>[\d\.]+) s",
                                       use_re=True)["t"]

      time_diff = float(ec_end_time) - float(ec_start_time)
      return time_diff >= DELAY and time_diff <= DELAY + ERROR_MARGIN
