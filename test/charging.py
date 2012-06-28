# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Charging state machine unit test
#

import time

def consume_charge_state(helper):
      try:
          while True:
              helper.wait_output("Charge state \S+ -> \S+",
                                 use_re=True,
                                 timeout=1)
      except:
          pass

def wait_charge_state(helper, state):
      helper.wait_output("Charge state \S+ -> %s" % state, use_re=True)

def test(helper):
      helper.wait_output("--- UART initialized")

      # Check charge when AC present
      consume_charge_state(helper)
      helper.ec_command("gpiomock AC_PRESENT 1")
      wait_charge_state(helper, "charge")

      # Check discharge when AC not present
      helper.ec_command("gpiomock AC_PRESENT 0")
      wait_charge_state(helper, "discharge")

      # Check charge current
      helper.ec_command("sbmock desire_current 2800")
      helper.ec_command("gpiomock AC_PRESENT 1")
      helper.wait_output("Charger set current: 2800")

      # Check charger voltage
      helper.ec_command("gpiomock AC_PRESENT 0")
      wait_charge_state(helper, "discharge")
      helper.ec_command("sbmock desire_voltage 7500")
      helper.ec_command("gpiomock AC_PRESENT 1")
      helper.wait_output("Charger set voltage: 7500")

      # While powered on and discharging, over-temperature should trigger
      # system shutdown
      helper.ec_command("gpiomock AC_PRESENT 0")
      wait_charge_state(helper, "discharge")
      helper.ec_command("powermock on")
      helper.ec_command("sbmock temperature 3700")
      helper.wait_output("Force shutdown")
      helper.ec_command("sbmock temperature 2981")
      time.sleep(1)

      # While powered on and discharging, under-temperature should trigger
      # system shutdown
      helper.ec_command("powermock on")
      helper.ec_command("sbmock temperature 2600")
      helper.wait_output("Force shutdown")
      helper.ec_command("sbmock temperature 2981")

      # While powered on and charging, over-temperature should stop battery
      # from charging
      consume_charge_state(helper)
      helper.ec_command("gpiomock AC_PRESENT 1")
      wait_charge_state(helper, "charge")
      helper.ec_command("powermock on")
      helper.ec_command("sbmock temperature 3700")
      wait_charge_state(helper, "idle")
      helper.ec_command("sbmock temperature 2981")
      wait_charge_state(helper, "charge")

      # While powered on and charging, under-temperature should stop battery
      # from charging
      helper.ec_command("sbmock temperature 2600")
      wait_charge_state(helper, "idle")
      helper.ec_command("sbmock temperature 2981")
      wait_charge_state(helper, "charge")

      return True # PASS !
