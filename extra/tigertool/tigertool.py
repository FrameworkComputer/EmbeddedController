#!/usr/bin/python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to control tigertail USB-C Mux board."""

import argparse
import sys
import time

import ecusb.tiny_servo_common as c

STM_VIDPID = '18d1:5027'
serialno = 'Uninitialized'

def do_mux(mux, pty):
  """Set mux via ec console 'pty'.

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

def do_reboot(pty):
  """Reboot via ec console pty

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
  group = parser.add_mutually_exclusive_group()
  group.add_argument('--setserialno', type=str, default=None,
                     help='serial number to set on the board.')
  group.add_argument('-m', '--mux', type=str, default=None,
                     help='mux selection')
  group.add_argument('-r', '--sysjump', type=str, default=None,
                     help='region selection')
  group.add_argument('--reboot', action='store_true',
                     help='reboot tigertail')
  return parser

def main(argv):
  parser = get_parser()
  opts = parser.parse_args(argv)

  result = True

  # Let's make sure there's a tigertail
  # If nothing found in 5 seconds, fail.
  c.wait_for_usb(STM_VIDPID, 5.)

  pty = c.setup_tinyservod(STM_VIDPID, 0, serialno=opts.serialno)

  if opts.setserialno:
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

  if result:
    c.log('PASS')
  else:
    c.log('FAIL')
    exit(-1)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
