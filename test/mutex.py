# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Mutexes test
#

def test(helper):
      helper.wait_output("[Mutex main task")

      # 3 locking in a row without contention
      helper.wait_output("No contention :done.")

      # serialization (simple contention)
      helper.wait_output("Simple contention :")
      helper.wait_output("MTX2: locking...done")
      helper.wait_output("MTX1: blocking...")
      helper.wait_output("MTX1: get lock")

      # multiple contention
      helper.wait_output("Massive locking/unlocking :")
      #TODO check sequence
      helper.wait_output("Test done.")

      return True # PASS !
