#!/usr/bin/env python
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to control tigertail USB-C Mux board."""

# Note: This is a py2/3 compatible file.

import argparse
import sys
import time

import ecusb.tiny_servo_common as c

STM_VIDPID = '18d1:5027'
serialno = 'Uninitialized'

def do_mux(mux, pty):
  """Set mux via ec console 'pty'.

  Args:
    mux: mux to connect to DUT, 'A', 'B', or 'off'
    pty: a pty object connected to tigertail

  Commands are:
  # > mux A
  # TYPE-C mux is A
  """
  validmux = ['A', 'B', 'off']
  if mux not in validmux:
    c.log('Mux setting %s invalid, try one of %s' % (mux, validmux))
    return False

  cmd = '\r\nmux %s\r\n' % mux
  regex = 'TYPE\-C mux is ([^\s\r\n]*)\r'

  results = pty._issue_cmd_get_results(cmd, [regex])[0]
  result = results[1].strip().strip('\n\r')

  if result != mux:
    c.log('Mux set to %s but saved as %s.' % (mux, result))
    return False
  c.log('Mux set to %s' % result)
  return True

def do_version(pty):
  """Check version via ec console 'pty'.

  Args:
    pty: a pty object connected to tigertail

  Commands are:
  # > version
  # Chip:    stm stm32f07x
  # Board:   0
  # RO:      tigertail_v1.1.6749-74d1a312e
  # RW:      tigertail_v1.1.6749-74d1a312e
  # Build:   tigertail_v1.1.6749-74d1a312e
  #          2017-07-25 20:08:34 nsanders@meatball.mtv.corp.google.com

  """
  cmd = '\r\nversion\r\n'
  regex = 'RO:\s+(\S+)\s+RW:\s+(\S+)\s+Build:\s+(\S+)\s+' \
          '(\d\d\d\d-\d\d-\d\d \d\d:\d\d:\d\d) (\S+)'

  results = pty._issue_cmd_get_results(cmd, [regex])[0]
  c.log('Version is %s' % results[3])
  c.log('RO:    %s' % results[1])
  c.log('RW:    %s' % results[2])
  c.log('Date:  %s' % results[4])
  c.log('Src:   %s' % results[5])

  return True

def do_check_serial(pty):
  """Check serial via ec console 'pty'.

  Args:
    pty: a pty object connected to tigertail

  Commands are:
  # > serialno
  # Serial number: number
  """
  cmd = '\r\nserialno\r\n'
  regex = 'Serial number: ([^\n\r]+)'

  results = pty._issue_cmd_get_results(cmd, [regex])[0]
  c.log('Serial is %s' % results[1])

  return True


def do_power(count, bus, pty):
  """Check power usage via ec console 'pty'.

  Args:
    count: number of samples to capture
    bus: rail to monitor, 'vbus', 'cc1', or 'cc2'
    pty: a pty object connected to tigertail

  Commands are:
  # > ina 0
  # Configuration: 4127
  # Shunt voltage: 02c4 => 1770 uV
  # Bus voltage  : 1008 => 5130 mV
  # Power        : 0019 => 625 mW
  # Current      : 0082 => 130 mA
  # Calibration  : 0155
  # Mask/Enable  : 0008
  # Alert limit  : 0000
  """
  if bus == 'vbus':
    ina = 0
  if bus == 'cc1':
    ina = 4
  if bus == 'cc2':
    ina = 1

  start = time.time()

  c.log('time,\tmV,\tmW,\tmA')

  cmd = '\r\nina %s\r\n' % ina
  regex = 'Bus voltage  : \S+ \S+ (\d+) mV\s+' \
          'Power        : \S+ \S+ (\d+) mW\s+' \
          'Current      : \S+ \S+ (\d+) mA'

  for i in range(0, count):
    results = pty._issue_cmd_get_results(cmd, [regex])[0]
    c.log('%.2f,\t%s,\t%s\t%s' % (time.time() - start,
          results[1], results[2], results[3]))

  return True

def do_reboot(pty):
  """Reboot via ec console pty

  Args:
    pty: a pty object connected to tigertail

  Command is: reboot.
  """
  cmd = '\r\nreboot\r\n'
  regex = 'Rebooting'

  try:
    results = pty._issue_cmd_get_results(cmd, [regex])[0]
    time.sleep(1)
    c.log(results)
  except Exception as e:
    c.log(e)
    return False

  return True

def do_sysjump(region, pty):
  """Set region via ec console 'pty'.

  Args:
    region: ec code region to execute, 'ro' or 'rw'
    pty: a pty object connected to tigertail

  Commands are:
  # > sysjump rw
  """
  validregion = ['ro', 'rw']
  if region not in validregion:
    c.log('Region setting %s invalid, try one of %s' % (
        region, validregion))
    return False

  cmd = '\r\nsysjump %s\r\n' % region
  try:
    pty._issue_cmd(cmd)
    time.sleep(1)
  except Exception as e:
    c.log(e)
    return False

  c.log('Region requested %s' % region)
  return True

def get_parser():
  parser = argparse.ArgumentParser(
      description=__doc__)
  parser.add_argument('-s', '--serialno', type=str, default=None,
                      help='serial number of board to use')
  parser.add_argument('-b', '--bus', type=str, default='vbus',
                     help='Which rail to log: [vbus|cc1|cc2]')
  group = parser.add_mutually_exclusive_group()
  group.add_argument('--setserialno', type=str, default=None,
                     help='serial number to set on the board.')
  group.add_argument('--check_serial', action='store_true',
                     help='check serial number set on the board.')
  group.add_argument('-m', '--mux', type=str, default=None,
                     help='mux selection')
  group.add_argument('-p', '--power', action='store_true',
                     help='check VBUS')
  group.add_argument('-l', '--powerlog', type=int, default=None,
                     help='log VBUS')
  group.add_argument('-r', '--sysjump', type=str, default=None,
                     help='region selection')
  group.add_argument('--reboot', action='store_true',
                     help='reboot tigertail')
  group.add_argument('--check_version', action='store_true',
                     help='check tigertail version')
  return parser

def main(argv):
  parser = get_parser()
  opts = parser.parse_args(argv)

  result = True

  # Let's make sure there's a tigertail
  # If nothing found in 5 seconds, fail.
  c.wait_for_usb(STM_VIDPID, timeout=5., serialname=opts.serialno)

  pty = c.setup_tinyservod(STM_VIDPID, 0, serialname=opts.serialno)

  if opts.bus not in ('vbus', 'cc1', 'cc2'):
    c.log('Try --bus [vbus|cc1|cc2]')
    result = False

  elif opts.setserialno:
    try:
      c.do_serialno(opts.setserialno, pty)
    except Exception:
      result = False

  elif opts.mux:
    result &= do_mux(opts.mux, pty)

  elif opts.sysjump:
    result &= do_sysjump(opts.sysjump, pty)

  elif opts.reboot:
    result &= do_reboot(pty)

  elif opts.check_version:
    result &= do_version(pty)

  elif opts.check_serial:
    result &= do_check_serial(pty)

  elif opts.power:
    result &= do_power(1, opts.bus, pty)

  elif opts.powerlog:
    result &= do_power(opts.powerlog, opts.bus, pty)

  if result:
    c.log('PASS')
  else:
    c.log('FAIL')
    exit(-1)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
