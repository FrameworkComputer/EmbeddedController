#!/usr/bin/env python3
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Ignore indention messages, since legacy scripts use 2 spaces instead of 4.
# pylint: disable=bad-indentation,docstring-section-indent
# pylint: disable=docstring-trailing-quotes

"""Smoke test of tigertool binary."""

import argparse
import subprocess
import sys


# Script to control tigertail USB-C Mux board.
#
# optional arguments:
#  -h, --help            show this help message and exit
#  -s SERIALNO, --serialno SERIALNO
#                        serial number of board to use
#  -b BUS, --bus BUS     Which rail to log: [vbus|cc1|cc2]
#  --setserialno SETSERIALNO
#                        serial number to set on the board.
#  --check_serial        check serial number set on the board.
#  -m MUX, --mux MUX     mux selection
#  -p, --power           check VBUS
#  -l POWERLOG, --powerlog POWERLOG
#                        log VBUS
#  -r SYSJUMP, --sysjump SYSJUMP
#                        region selection
#  --reboot              reboot tigertail
#  --check_version       check tigertail version


def testCmd(cmd, expected_results):
  """Run command on console, check for success.

  Args:
    cmd: shell command to run.
    expected_results: a list object of strings expected in the result.

  Raises:
    Exception on fail.
  """
  print('run: ' + cmd)
  try:
    p = subprocess.run(cmd, shell=True, check=False, capture_output=True)
    output = p.stdout.decode('utf-8')
    error = p.stderr.decode('utf-8')
    assert p.returncode == 0
    for result in expected_results:
      output.index(result)
  except Exception as e:
    print('FAIL')
    print('cmd: ' + cmd)
    print('error: ' + str(e))
    print('stdout:\n' + output)
    print('stderr:\n' + error)
    print('expected: ' + str(expected_results))
    print('RC: ' + str(p.returncode))
    raise e

def test_sequence():
  testCmd('./tigertool.py --reboot', ['PASS'])
  testCmd('./tigertool.py --setserialno test', ['PASS'])
  testCmd('./tigertool.py --check_serial', ['test', 'PASS'])
  testCmd('./tigertool.py -s test --check_serial', ['test', 'PASS'])
  testCmd('./tigertool.py -m A', ['Mux set to A', 'PASS'])
  testCmd('./tigertool.py -m B', ['Mux set to B', 'PASS'])
  testCmd('./tigertool.py -m off', ['Mux set to off', 'PASS'])
  testCmd('./tigertool.py -p', ['PASS'])
  testCmd('./tigertool.py -r rw', ['PASS'])
  testCmd('./tigertool.py -r ro', ['PASS'])
  testCmd('./tigertool.py --check_version', ['RW', 'RO', 'PASS'])

  print('PASS')

def main(argv):
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('-c', '--count', type=int, default=1,
                      help='loops to run')

  opts = parser.parse_args(argv)

  for i in range(1, opts.count + 1):
    print('Iteration: %d' % i)
    test_sequence()

if __name__ == '__main__':
    main(sys.argv[1:])
