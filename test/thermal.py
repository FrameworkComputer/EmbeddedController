# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Thermal engine unit test
#

CPU = 0
BOARD = 1
CASE = 2

def getWarningConfig(helper, sensor_type):
      ret = dict()
      helper.ec_command("thermalconf %d" % sensor_type)
      ret['warning'] = int(helper.wait_output(
          "Warning:\s*(?P<t>\d+) K", use_re=True)["t"])
      ret['cpudown'] = int(helper.wait_output(
          "CPU Down:\s*(?P<t>\d+) K", use_re=True)["t"])
      ret['powerdown'] = int(helper.wait_output(
          "Power Down:\s*(?P<t>\d+) K", use_re=True)["t"])
      return ret

def getFanConfig(helper, sensor_type):
      ret = list()
      helper.ec_command("thermalfan %d" % sensor_type)
      while True:
          try:
              match = helper.wait_output("(?P<t>\d+)\s*K:\s*(?P<r>-?\d+)\s",
                                         use_re=True, timeout=1)
              ret.append((int(match["t"]), int(match["r"])))
          except:
              break
      return ret


def test(helper):
      helper.wait_output("Inits done")

      # Get thermal engine configuration
      config = [getWarningConfig(helper, sensor_type)
                for sensor_type in xrange(3)]
      fan_config = [getFanConfig(helper, sensor_type)
                    for sensor_type in xrange(3)]

      # Set initial temperature values
      helper.ec_command("setcputemp %d" % max(fan_config[CPU][0][0]-1, 0))
      helper.ec_command("setboardtemp %d" % max(fan_config[BOARD][0][0]-1, 0))
      helper.ec_command("setcasetemp %d" % max(fan_config[CASE][0][0]-1, 0))

      # Increase CPU temperature to first fan step
      # Check if fan comes up
      helper.ec_command("setcputemp %d" % fan_config[CPU][0][0])
      helper.wait_output("Fan RPM: %d" % fan_config[CPU][0][1], timeout=11)

      # Increase CPU temperature to second fan step
      helper.ec_command("setcputemp %d" % fan_config[CPU][1][0])
      helper.wait_output("Fan RPM: %d" % fan_config[CPU][1][1], timeout=11)

      # Lower CPU temperature to 1 degree below second fan step
      # Check fan speed doesn't change
      helper.ec_command("setcputemp %d" % (fan_config[CPU][1][0]-1))
      for i in xrange(12):
          helper.wait_output("Fan RPM: %d" % fan_config[CPU][1][1], timeout=2)

      # Set CPU temperature to a high value for only one second
      # Check fan speed doesn't change
      helper.ec_command("setcputemp 400")
      helper.wait_output("Fan RPM: %d" % fan_config[CPU][1][1])
      helper.ec_command("setcputemp %d" % fan_config[CPU][1][0])

      # Set case temperature to first fan step
      # Check fan is set to second step
      helper.ec_command("setcasetemp %d" % fan_config[CASE][0][0])
      for i in xrange(12):
          helper.wait_output("Fan RPM: %d" % fan_config[CASE][1][1], timeout=2)

      # Set case temperature to third fan step
      # Check fan is set to third step
      helper.ec_command("setcasetemp %d" % fan_config[CASE][2][0])
      helper.wait_output("Fan RPM: %d" % fan_config[CASE][2][1], timeout=11)

      # Set CPU temperature to trigger warning and throttle CPU
      helper.ec_command("setcputemp %d" % config[CPU]['warning'])
      helper.wait_output("Throttle CPU.", timeout=11)
      helper.wait_output("Host event: 200", timeout=2)

      # Lower CPU temperature and check CPU is not throttled
      helper.ec_command("setcputemp %d" % (config[CPU]['warning']-5))
      helper.wait_output("No longer throttle CPU.", timeout=2)

      # Set CPU temperature to trigger CPU shutdown
      helper.ec_command("setcputemp %d" % config[CPU]['cpudown'])
      helper.wait_output("CPU overheated", timeout=11)

      # Set CPU temperature to trigger force shutdown
      helper.ec_command("setcputemp %d" % config[CPU]['powerdown'])
      helper.wait_output("Force shutdown", timeout=11)

      # Pass!
      return True
