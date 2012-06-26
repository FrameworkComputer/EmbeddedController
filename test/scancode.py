# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# i8042 scancode test
#

def test(helper):
      # Wait for EC initialized
      helper.wait_output("--- UART initialized")

      # Disable typematic
      helper.ec_command("typematic 1000000 1000000")

      # Enable XLATE (Scan code set 1)
      helper.ec_command("ctrlram 0 0x40")
      helper.ec_command("mockmatrix 1 1 1")
      helper.wait_output("i8042 SEND: 01") # make code
      helper.ec_command("mockmatrix 1 1 0")
      helper.wait_output("i8042 SEND: 81") # break code

      # Disable XLATE (Scan code set 2)
      helper.ec_command("ctrlram 0 0x00")
      helper.ec_command("mockmatrix 1 1 1")
      helper.wait_output("i8042 SEND: 76") # make code
      helper.ec_command("mockmatrix 1 1 0")
      helper.wait_output("i8042 SEND: f0 76") # break code

      return True # PASS !
