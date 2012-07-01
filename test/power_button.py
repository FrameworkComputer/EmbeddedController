# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Power button debounce test
#
# Refer to section 1.3 Power Button of
# https://sites.google.com/a/google.com/chromeos-partners/pages/
# tech-docs/firmware/ec-specification-v119
#

import time

SHORTER_THAN_T0 = 0.01
LONGER_THAN_T0 = 0.05
LONGER_THAN_T1 = 5

def consume_output(helper, reg_ex):
    done = False
    while not done:
        try:
            helper.wait_output(reg_ex, use_re=True, timeout=1)
        except:
            done = True

def test(helper):
      helper.wait_output("--- UART initialized")
      # Release power button, set to soft off, and enable keyboard
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.ec_command("powermock off")
      helper.ec_command("kbd enable")
      consume_output(helper, "PB released")

      helper.trace("Press power button for shorter than T0 and check this\n" +
                   "event is ignored\n")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      if not helper.check_no_output("PB released"):
          return False

      helper.trace("Press power button for longer than T0 and check this\n" +
                   "event is treated as a single press.")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.wait_output("PB released", timeout=1)
      # Expect shown only once
      if not helper.check_no_output("PB released"):
          return False

      helper.trace("Press power button for two consecutive SHORTER_THAN_T0\n" +
                   "period and check this event is ignored\n")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      if not helper.check_no_output("PB released"):
          return False

      helper.trace("Hold down power button for LONGER_THAN_T0 and check a\n" +
                   "single press is sent out\n")
      consume_output(helper, "pwrbtn=")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.wait_output("pwrbtn=LOW", timeout=1)
      helper.wait_output("pwrbtn=HIGH", timeout=1)
      if not helper.check_no_output("pwrbtn=LOW"):
          return False

      helper.trace("Press power button for SHORTER_THAN_T0, release for\n" +
                   "SHORTER_THAN_T0, and then press for LONGER_THAN_T0.\n" +
                   "Check this is treated as a single press\n")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.wait_output("pwrbtn=LOW", timeout=1)
      helper.wait_output("pwrbtn=HIGH", timeout=1)
      if not helper.check_no_output("pwrbtn=LOW"):
          return False

      helper.trace("Hold down power button, wait for power button press\n" +
                   "sent out. Then relase for SHORTER_THAN_T0, check power\n" +
                   "button release is not sent out. Expect power button is\n" +
                   "treated as hold (ignoring the relase bounce)\n")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      helper.wait_output("pwrbtn=LOW", timeout=1)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      time.sleep(SHORTER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      if not helper.check_no_output("PB released"):
          return False
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.wait_output("PB released", timeout=1)

      helper.trace("When system is off, hold down power button for\n" +
                   "LONGER_THAN_T0. Check the initial is stretched\n")
      consume_output(helper, "pwrbtn=")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      t_low = helper.wait_output("\[(?P<t>[\d\.]+) PB PCH pwrbtn=LOW\]",
                                 use_re=True)["t"]
      t_high = helper.wait_output("\[(?P<t>[\d\.]+) PB PCH pwrbtn=HIGH\]",
                                  use_re=True)["t"]
      if not helper.check_no_output("pwrbtn=LOW"):
          return False
      if float(t_high) - float(t_low) <= LONGER_THAN_T0 - 0.1:
          return False

      helper.trace("When system is off, hold down power button for\n" +
                   "LONGER_THAN_T0. Check no scan code is send\n")
      consume_output(helper, "i8042 SEND")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      if not helper.check_no_output("i8042 SEND"):
          return False

      helper.trace("While powered on, hold down power button for\n" +
                   "LONGER_THAN_T0. A single short pulse should be sent\n")
      consume_output(helper, "pwrbtn=")
      helper.ec_command("powermock on")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      t_low = helper.wait_output("\[(?P<t>[\d\.]+) PB PCH pwrbtn=LOW\]",
                                 use_re=True)["t"]
      t_high = helper.wait_output("\[(?P<t>[\d\.]+) PB PCH pwrbtn=HIGH\]",
                                  use_re=True)["t"]
      if not helper.check_no_output("pwrbtn=LOW"):
          return False
      if float(t_high) - float(t_low) >= 0.1:
          return False

      helper.trace("While powered on, hold down power button for\n" +
                   "LONGER_THAN_T0. Scan code should be sent\n")
      consume_output(helper, "i8042 SEND")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      helper.wait_output("i8042 SEND", timeout=1) # Expect make code
      time.sleep(LONGER_THAN_T0)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.wait_output("i8042 SEND", timeout=1) # Expect release code

      helper.trace("While powered on, hold down power button for\n" +
                   "LONGER_THAN_T1 and check two presses are sent out\n")
      consume_output(helper, "pwrbtn=")
      helper.ec_command("gpiomock POWER_BUTTONn 0")
      time.sleep(LONGER_THAN_T1)
      helper.ec_command("gpiomock POWER_BUTTONn 1")
      helper.wait_output("pwrbtn=LOW", timeout=1)
      helper.wait_output("pwrbtn=HIGH", timeout=1)
      helper.wait_output("pwrbtn=LOW", timeout=1)
      helper.wait_output("pwrbtn=HIGH", timeout=1)
      if not helper.check_no_output("pwrbtn=LOW"):
          return False

      return True # PASS !
