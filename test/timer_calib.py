# Copyright 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Check timers behavior
#

import time

def one_pass(helper):
      helper.wait_output("=== Timer calibration ===")
      res = helper.wait_output("back-to-back get_time : (?P<lat>[0-9]+) us",
                               use_re=True)["lat"]
      minlat = int(res)
      helper.trace("get_time latency %d us\n" % minlat)

      helper.wait_output("sleep 1s")
      t0 = time.time()
      second = helper.wait_output("done. delay = (?P<second>[0-9]+) us",
                               use_re=True)["second"]
      t1 = time.time()
      secondreal = t1 - t0
      secondlat = int(second) - 1000000
      helper.trace("1s timer latency %d us / real time %f s\n" % (secondlat,
                                                                  secondreal))


      us = {}
      for pow2 in range(7):
          delay = 1 << (7-pow2)
          us[delay] = helper.wait_output("%d us => (?P<us>[0-9]+) us" % delay,
                                  use_re=True)["us"]
      helper.wait_output("Done.")

      return minlat, secondlat, secondreal


def test(helper):
      one_pass(helper)

      helper.ec_command("reboot")
      helper.wait_output("--- UART initialized")

      # get the timing results on the second pass
      # to avoid binary translation overhead
      minlat, secondlat, secondreal = one_pass(helper)

      # check that the timings somewhat make sense
      if minlat > 220 or secondlat > 500 or abs(secondreal-1.0) > 0.200:
           helper.fail("imprecise timings " +
                       "(get_time %d us sleep %d us / real time %.3f s)" %
                       (minlat, secondlat, secondreal))

      return True # PASS !
