# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Simple test as an example
#

def test(helper):
      helper.wait_output("--- Chrome EC initialized! ---")
      helper.wait_prompt()
      helper.ec_command("version")
      ro = helper.wait_output("RO version:\s*(?P<ro>\S+)", use_re=True)["ro"]
      wa = helper.wait_output("RW-A version:\s*(?P<a>\S+)", use_re=True)["a"]
      wb = helper.wait_output("RW-B version:\s*(?P<b>\S+)", use_re=True)["b"]
      helper.trace("Version (RO/A/B) %s / %s / %s\n" % (ro, wa, wb))
      return True # PASS !
