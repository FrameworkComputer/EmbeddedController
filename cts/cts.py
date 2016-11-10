#!/usr/bin/python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script which builds, flashes, and runs EC CTS
#
# Software prerequisites:
# - openocd version 0.10 or above
# - lsusb
# - udevadm
#
# To try it out, hook two boards (DEFAULT_TH and DEFAULT_DUT) with USB cables
# to the host and execute the script:
#   $ ./cts.py
# It'll run mock tests. The result will be stored in CTS_TEST_RESULT_DIR.


import argparse
from collections import defaultdict
import common.board as board
import os
import shutil
import time


# Host only return codes. Make sure they match values in cts.rc
CTS_RC_DUPLICATE_RUN = -2  # The test was run multiple times.
CTS_RC_NO_RESULT = -1  # The test did not run.

DEFAULT_TH = 'stm32l476g-eval'
DEFAULT_DUT = 'nucleo-f072rb'
MAX_SUITE_TIME_SEC = 5
CTS_TEST_RESULT_DIR = '/tmp/ects'


class Cts(object):
  """Class that represents a CTS testing setup and provides
  interface to boards (building, flashing, etc.)

  Attributes:
    dut: DeviceUnderTest object representing dut
    th: TestHarness object representing th
    module: Name of module to build/run tests for
    test_names: List of strings of test names contained in given module
    return_codes: Dict of strings of return codes, with a code's integer
      value being the index for the corresponding string representation
  """

  def __init__(self, ec_dir, th, dut, module):
    """Initializes cts class object with given arguments.

    Args:
      ec_dir: Path to ec directory
      th: Name of the test harness board
      dut: Name of the device under test board
      module: Name of module to build/run tests for (e.g. gpio, interrupt)
    """
    self.results_dir = os.path.join(CTS_TEST_RESULT_DIR, dut, module)
    if os.path.isdir(self.results_dir):
      shutil.rmtree(self.results_dir)
    else:
      os.makedirs(self.results_dir)
    self.ec_dir = ec_dir
    self.module = module
    serial_path = os.path.join(CTS_TEST_RESULT_DIR, 'th_serial')
    self.th = board.TestHarness(th, module, self.results_dir, serial_path)
    self.dut = board.DeviceUnderTest(dut, self.th, module, self.results_dir)
    cts_dir = os.path.join(self.ec_dir, 'cts')
    testlist_path = os.path.join(cts_dir, self.module, 'cts.testlist')
    self.test_names = Cts.get_macro_args(testlist_path, 'CTS_TEST')

    return_codes_path = os.path.join(cts_dir, 'common', 'cts.rc')
    self.get_return_codes(return_codes_path, 'CTS_RC_')

  def build(self):
    """Build images for DUT and TH"""
    print 'Building DUT image...'
    if not self.dut.build(self.module, self.ec_dir):
      raise RuntimeError('Building module %s for DUT failed' % (self.module))
    print 'Building TH image...'
    if not self.th.build(self.module, self.ec_dir):
      raise RuntimeError('Building module %s for TH failed' % (self.module))

  def flash_boards(self):
    """Flashes th and dut boards with their most recently build ec.bin"""
    cts_module = 'cts_' + self.module
    image_path = os.path.join('build', self.th.board, cts_module, 'ec.bin')
    self.identify_boards()
    print 'Flashing TH with', image_path
    if not self.th.flash(image_path):
      raise RuntimeError('Flashing TH failed')
    image_path = os.path.join('build', self.dut.board, cts_module, 'ec.bin')
    print 'Flashing DUT with', image_path
    if not self.dut.flash(image_path):
      raise RuntimeError('Flashing DUT failed')

  def setup(self):
    """Setup boards"""
    self.th.save_serial()

  def identify_boards(self):
    """Updates serials of both th and dut, in that order (order matters)"""
    self.th.get_serial()
    self.dut.get_serial()

  @staticmethod
  def get_macro_args(filepath, macro):
    """Get list of args of a certain macro in a file when macro is used
    by itself on a line

    Args:
      filepath: String containing absolute path to the file
      macro: String containing text of macro to get args of
    """
    args = []
    with open(filepath, 'r') as f:
      for l in f.readlines():
        if not l.strip().startswith(macro):
          continue
        l = l.strip()[len(macro):]
        args.append(l.strip('()').replace(',', ''))
    return args

  def get_return_codes(self, file, prefix):
    """Extract return code names from the definition file (cts.rc)"""
    self.return_codes = {}
    val = 0
    with open(file, 'r') as f:
      for line in f.readlines():
        line = line.strip()
        if not line.startswith(prefix):
          continue
        line = line[len(prefix):]
        line = line.split(',')[0]
        if '=' in line:
          tokens = line.split('=')
          line = tokens[0].strip()
          val = int(tokens[1].strip())
        self.return_codes[val] = line
        val += 1

  def parse_output(self, output):
    results = defaultdict(lambda: CTS_RC_NO_RESULT)

    for ln in [ln.strip() for ln in output.split('\n')]:
      tokens = ln.split()
      if len(tokens) != 2:
        continue
      test_name = tokens[0].strip()
      if test_name not in self.test_names:
        continue
      try:
        return_code = int(tokens[1])
      except ValueError: # Second token is not an int
        continue
      if test_name in results:
        results[test_name] = CTS_RC_DUPLICATE_RUN
      else:
        results[test_name] = return_code

    return results

  def get_return_code_name(self, code):
    return self.return_codes.get(code, '%d' % code)

  def evaluate_run(self, dut_output, th_output):
    """Parse outputs to derive test results

    Args;
      dut_output: String output of DUT
      th_output: String output of TH
    """
    dut_results = self.parse_output(dut_output)
    th_results = self.parse_output(th_output)

    len_test_name = max(len(s) for s in self.test_names)
    len_code_name = max(len(s) for s in self.return_codes.values())

    head = '{:^' + str(len_test_name) + '} '
    head += '{:^' + str(len_code_name) + '} '
    head += '{:^' + str(len_code_name) + '}\n'
    fmt = '{:' + str(len_test_name) + '} '
    fmt += '{:>' + str(len_code_name) + '} '
    fmt += '{:>' + str(len_code_name) + '}\n'

    self.formatted_results = head.format('test name', 'TH', 'DUT')
    for test_name in self.test_names:
      th_cn = self.get_return_code_name(th_results[test_name])
      dut_cn = self.get_return_code_name(dut_results[test_name])
      self.formatted_results += fmt.format(test_name, th_cn, dut_cn)

  def run(self):
    """Resets boards, records test results in results dir"""
    print 'Reading serials...'
    self.identify_boards()
    print 'Opening DUT tty...'
    self.dut.setup_tty()
    print 'Opening TH tty...'
    self.th.setup_tty()

    # Boards might be still writing to tty. Wait a few seconds before flashing.
    time.sleep(3)

    # clear buffers
    print 'Clearing DUT tty...'
    self.dut.read_tty()
    print 'Clearing TH tty...'
    self.th.read_tty()

    # Resets the boards and allows them to run tests
    # Due to current (7/27/16) version of sync function,
    # both boards must be rest and halted, with the th
    # resuming first, in order for the test suite to run in sync
    print 'Halting TH...'
    if not self.th.send_open_ocd_commands(['init', 'reset halt']):
      raise RuntimeError('Failed to halt TH')
    print 'Halting DUT...'
    if not self.dut.send_open_ocd_commands(['init', 'reset halt']):
      raise RuntimeError('Failed to halt DUT')
    print 'Resuming TH...'
    if not self.th.send_open_ocd_commands(['init', 'resume']):
      raise RuntimeError('Failed to resume TH')
    print 'Resuming DUT...'
    if not self.dut.send_open_ocd_commands(['init', 'resume']):
      raise RuntimeError('Failed to resume DUT')

    time.sleep(MAX_SUITE_TIME_SEC)

    print 'Reading DUT tty...'
    dut_output, dut_boot = self.dut.read_tty()
    print 'Reading TH tty...'
    th_output, th_boot = self.th.read_tty()

    print 'Halting TH...'
    if not self.th.send_open_ocd_commands(['init', 'reset halt']):
      raise RuntimeError('Failed to halt TH')
    print 'Halting DUT...'
    if not self.dut.send_open_ocd_commands(['init', 'reset halt']):
      raise RuntimeError('Failed to halt DUT')

    if not dut_output or not th_output:
      raise ValueError('Output missing from boards. If you have a process '
                       'reading ttyACMx, please kill that process and try '
                       'again.')

    print 'Pursing results...'
    self.evaluate_run(dut_output, th_output)

    # Write results
    dest = os.path.join(self.results_dir, 'results.log')
    with open(dest, 'w') as fl:
      fl.write(self.formatted_results)

    # Write UART outputs
    dest = os.path.join(self.results_dir, 'uart_th.log')
    with open(dest, 'w') as fl:
      fl.write(th_output)
    dest = os.path.join(self.results_dir, 'uart_dut.log')
    with open(dest, 'w') as fl:
      fl.write(dut_output)

    print self.formatted_results

    # TODO: Should set exit code for the shell


def main():
  """Main entry point for CTS script from command line"""
  ec_dir = os.path.realpath(os.path.join(
      os.path.dirname(os.path.abspath(__file__)), '..'))
  os.chdir(ec_dir)

  dut = DEFAULT_DUT
  module = 'meta'

  parser = argparse.ArgumentParser(description='Used to build/flash boards')
  parser.add_argument('-d',
                      '--dut',
                      help='Specify DUT you want to build/flash')
  parser.add_argument('-m',
                      '--module',
                      help='Specify module you want to build/flash')
  parser.add_argument('-s',
                      '--setup',
                      action='store_true',
                      help='Connect only the TH to save its serial')
  parser.add_argument('-b',
                      '--build',
                      action='store_true',
                      help='Build test suite (no flashing)')
  parser.add_argument('-f',
                      '--flash',
                      action='store_true',
                      help='Flash boards with most recent images')
  parser.add_argument('-r',
                      '--run',
                      action='store_true',
                      help='Run tests without flashing')

  args = parser.parse_args()

  if args.module:
    module = args.module

  if args.dut:
    dut = args.dut

  cts = Cts(ec_dir, DEFAULT_TH, dut=dut, module=module)

  if args.setup:
    cts.setup()
  elif args.build:
    cts.build()
  elif args.flash:
    cts.flash_boards()
  elif args.run:
    cts.run()
  else:
    cts.build()
    cts.flash_boards()
    cts.run()

if __name__ == "__main__":
  main()
