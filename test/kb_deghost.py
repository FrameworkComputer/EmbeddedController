# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Keyboard deghost test
#

import time

def test(helper):
      # Wait for EC initialized
      helper.wait_output("--- UART initialized")

      # Enable keyboard scanning and disable typematic
      helper.ec_command("kbd enable")
      helper.ec_command("typematic 1000000 1000000")

      # Press (1, 1) and (2, 2)
      helper.ec_command("mockmatrix 1 1 1")
      helper.wait_output("KB raw")
      helper.ec_command("mockmatrix 2 2 1")
      helper.wait_output("KB raw")

      # Now press (1, 2) which should cause (2, 1) to be pressed also
      # Expect this is ignored
      helper.ec_command("mockmatrix 2 1 1")
      helper.ec_command("mockmatrix 1 2 1")
      if not helper.check_no_output("KB raw"):
          return False
      # Now release (1, 2) which should cause (2, 1) to be released also
      # Expect this is ignored
      helper.ec_command("mockmatrix 2 1 0")
      helper.ec_command("mockmatrix 1 2 0")
      if not helper.check_no_output("KB raw"):
          return False

      # Done testing with (1, 1) and (2, 2). Release them.
      helper.ec_command("mockmatrix 1 1 0")
      helper.wait_output("KB raw")
      helper.ec_command("mockmatrix 2 2 0")
      helper.wait_output("KB raw")

      # Press (0, 2) and (1, 1)
      helper.ec_command("mockmatrix 0 2 1")
      helper.wait_output("KB raw")
      helper.ec_command("mockmatrix 1 1 1")
      helper.wait_output("KB raw")

      # (0, 1) maps to no key. Pressing (1, 2) and (0, 1) should not be
      # deghosted.
      helper.ec_command("mockmatrix 1 2 1")
      helper.ec_command("mockmatrix 0 1 1")
      helper.wait_output("KB raw")

      return True # PASS !
