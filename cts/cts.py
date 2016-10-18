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
#   $ ./cts.py --debug
# It'll run mock tests. The result will be stored in CTS_TEST_RESULT_DIR.


import argparse
import collections
import os
import time
import common.board as board
from copy import deepcopy
import xml.etree.ElementTree as et
from twisted.python.syslog import DEFAULT_FACILITY


CTS_CORRUPTED_CODE = -2  # The test didn't execute correctly
CTS_CONFLICTING_CODE = -1 # Error codes should never conflict
CTS_SUCCESS_CODE = 0
CTS_COLOR_RED = '#fb7d7d'
CTS_COLOR_GREEN = '#7dfb9f'
DEFAULT_TH = 'stm32l476g-eval'
DEFAULT_DUT = 'nucleo-f072rb'
MAX_SUITE_TIME_SEC = 5
CTS_DEBUG_START = '[DEBUG]'
CTS_DEBUG_END = '[DEBUG_END]'
CTS_TEST_RESULT_DIR = '/tmp/cts'


class Cts(object):
  """Class that represents a CTS testing setup and provides
  interface to boards (building, flashing, etc.)

  Attributes:
    dut: DeviceUnderTest object representing dut
    th: TestHarness object representing th
    module: Name of module to build/run tests for
    test_names: List of strings of test names contained in given module
    test_results: Dictionary of results of each test from module, with
        keys being test name strings and values being test result integers
    return_codes: Dict of strings of return codes, with a code's integer
      value being the index for the corresponding string representation
    debug: Boolean that indicates whether or not on-board debug message
      printing should be enabled when building.
    debug_output: Dictionary mapping test name to an array contain debug
      messages sent while it was running
  """

  def __init__(self, ec_dir, dut, module, debug=False):
    """Initializes cts class object with given arguments.

    Args:
      dut: Name of Device Under Test (DUT) board
      ec_dir: String path to ec directory
      dut: Name of board to use for DUT
      module: Name of module to build/run tests for
      debug: Boolean that indicates whether or not on-board debug message
        printing should be enabled.
    """
    self.results_dir = CTS_TEST_RESULT_DIR
    self.ec_dir = ec_dir
    self.module = module
    self.debug = debug
    serial_path = os.path.join(self.ec_dir, 'build', 'cts_th_serial')
    self.th = board.TestHarness(DEFAULT_TH, serial_path)
    self.dut = board.DeviceUnderTest(dut, self.th)
    cts_dir = os.path.join(self.ec_dir, 'cts')
    testlist_path = os.path.join(cts_dir, self.module, 'cts.testlist')
    self.test_names = Cts.get_macro_args(testlist_path, 'CTS_TEST')

    self.debug_output = {}
    for test in self.test_names:
      self.debug_output[test] = []

    return_codes_path = os.path.join(cts_dir, 'common', 'cts.rc')
    self.return_codes = dict(enumerate(Cts.get_macro_args(
        return_codes_path, 'CTS_RC_')))

    self.return_codes[CTS_CONFLICTING_CODE] = 'RESULTS CONFLICT'
    self.return_codes[CTS_CORRUPTED_CODE] = 'CORRUPTED'
    self.test_results = collections.OrderedDict()

  def build(self):
    """Build images for DUT and TH"""
    if self.dut.build(self.module, self.ec_dir, self.debug):
      raise RuntimeError('Building module %s for DUT failed' % (self.module))
    if self.th.build(self.module, self.ec_dir, self.debug):
      raise RuntimeError('Building module %s for TH failed' % (self.module))

  def flash_boards(self):
    """Flashes th and dut boards with their most recently build ec.bin"""
    cts_module = 'cts_' + self.module
    image_path = os.path.join('build', self.th.board, cts_module, 'ec.bin')
    self.identify_boards()
    print 'Flashing TH with', image_path
    if self.th.flash(image_path):
      raise RuntimeError('Flashing TH failed')
    image_path = os.path.join('build', self.dut.board, cts_module, 'ec.bin')
    print 'Flashing DUT with', image_path
    if self.dut.flash(image_path):
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

  def extract_debug_output(self, output):
    """Append the debug messages from output to self.debug_output

    Args:
      output: String containing output from which to extract debug
        messages
    """
    lines = [ln.strip() for ln in output.split('\n')]
    test_num = 0
    i = 0
    message_buf = []
    while i < len(lines):
      if test_num >= len(self.test_names):
        break
      if lines[i].strip() == CTS_DEBUG_START:
        i += 1
        msg = ''
        while i < len(lines):
          if lines[i] == CTS_DEBUG_END:
            break
          else:
            msg += lines[i] + '\n'
            i += 1
        message_buf.append(msg)
      else:
        current_test = self.test_names[test_num]
        if lines[i].strip().startswith(current_test):
          self.debug_output[current_test] += message_buf
          message_buf = []
          test_num += 1
      i += 1

  def parse_output(self, r1, r2):
    """Parse the outputs of the DUT and TH together

    Args;
      r1: String output of one of the DUT or the TH (order does not matter)
      r2: String output of one of the DUT or the TH (order does not matter)
    """
    self.test_results.clear()  # empty out any old results

    first_corrupted_test = len(self.test_names)

    self.extract_debug_output(r1)
    self.extract_debug_output(r2)

    for output_str in [r1, r2]:
      test_num = 0
      for ln in [ln.strip() for ln in output_str.split('\n')]:
        tokens = ln.split()
        if len(tokens) != 2:
          continue
        test = tokens[0].strip()
        if test not in self.test_names:
          continue
        try:
          return_code = int(tokens[1])
        except ValueError: # Second token is not an int
          continue
        if test != self.test_names[test_num]:
          first_corrupted_test = test_num
          break # Results after this test are corrupted
        elif self.test_results.get(
            test,
            CTS_SUCCESS_CODE) == CTS_SUCCESS_CODE:
          self.test_results[test] = return_code
        elif return_code == CTS_SUCCESS_CODE:
          pass
        elif return_code != self.test_results[test]:
          self.test_results[test] = CTS_CONFLICTING_CODE
        test_num += 1

      if test_num != len(self.test_names): # If a suite didn't finish
        first_corrupted_test = min(first_corrupted_test, test_num)

    if first_corrupted_test < len(self.test_names):
      for test in self.test_names[first_corrupted_test:]:
        self.test_results[test] = CTS_CORRUPTED_CODE

  def _results_as_string(self):
    """Takes saved results and returns a duplicate of their dictionary
    with the return codes replaces with their string representation

    Returns:
      dictionary with test name strings as keys and test result strings
          as values
    """
    result = deepcopy(self.test_results)
    # Convert codes to strings
    for test, code in result.items():
        result[test] = self.return_codes.get(code, 'UNKNOWN %d' % code)
    return result

  def prettify_results(self):
    """Takes saved results and returns a string representation of them

    Return: Dictionary similar to self.test_results, but with strings
        instead of error codes
    """
    res = self._results_as_string()
    t_long = max(len(s) for s in res.keys())
    e_max_len = max(len(s) for s in res.values())

    pretty_results = 'CTS Test Results for ' + self.module + ' module:\n'

    for test, code in res.items():
      align_str = '\n{0:<' + str(t_long) + \
        '} {1:>' + str(e_max_len) + '}'
      pretty_results += align_str.format(test, code)

    return pretty_results

  def results_as_html(self):
    res = self._results_as_string()
    root = et.Element('html')
    head = et.SubElement(root, 'head')
    style = et.SubElement(head, 'style')
    style.text = ('table, td, th {border: 1px solid black;}'
                  'body {font-family: \"Lucida Console\", Monaco, monospace')
    body = et.SubElement(root, 'body')
    table = et.SubElement(body, 'table')
    table.set('style','width:100%')
    title_row = et.SubElement(table, 'tr')
    test_name_title = et.SubElement(title_row, 'th')
    test_name_title.text = 'Test Name'
    test_name_title.set('style', 'white-space : nowrap')
    test_results_title = et.SubElement(title_row, 'th')
    test_results_title.text = 'Test Result'
    test_results_title.set('style', 'white-space : nowrap')
    test_debug_title = et.SubElement(title_row, 'th')
    test_debug_title.text = 'Debug Output'
    test_debug_title.set('style', 'width:99%')

    for name, result in res.items():
      row = et.SubElement(table, 'tr')
      name_e = et.SubElement(row, 'td')
      name_e.text = name
      name_e.set('style', 'white-space : nowrap')
      result_e = et.SubElement(row, 'td')
      result_e.text = result
      result_e.set('style', 'white-space : nowrap')
      debug_e = et.SubElement(row, 'td')
      debug_e.set('style', 'width:99%')
      debug_e.set('style', 'white-space : pre-wrap')
      if len(self.debug_output[name]) == 0:
        debug_e.text = 'None'
      else:
        combined_message = ''
        for msg in self.debug_output[name]:
          combined_message += msg
        combined_message = combined_message
        debug_e.text = combined_message
      if result == self.return_codes[CTS_SUCCESS_CODE]:
        result_e.set('bgcolor', CTS_COLOR_GREEN)
      else:
        result_e.set('bgcolor', CTS_COLOR_RED)

    return et.tostring(root, method='html')

  def run(self):
    """Resets boards, records test results in results dir"""
    self.identify_boards()
    self.dut.setup_tty()
    self.th.setup_tty()

    # Boards might be still writing to tty. Wait a few seconds before flashing.
    time.sleep(3)

    # clear buffers
    self.dut.read_tty()
    self.th.read_tty()

    # Resets the boards and allows them to run tests
    # Due to current (7/27/16) version of sync function,
    # both boards must be rest and halted, with the th
    # resuming first, in order for the test suite to run in sync
    print 'Halting TH...'
    self.th.send_open_ocd_commands(['init', 'reset halt'])
    print 'Resetting DUT...'
    self.dut.send_open_ocd_commands(['init', 'reset halt'])
    print 'Resuming TH...'
    self.th.send_open_ocd_commands(['init', 'resume'])
    print 'Resuming DUT...'
    self.dut.send_open_ocd_commands(['init', 'resume'])

    time.sleep(MAX_SUITE_TIME_SEC)

    dut_results = self.dut.read_tty()
    th_results = self.th.read_tty()

    if not dut_results or not th_results:
      raise ValueError('Output missing from boards. If you have a process '
                       'reading ttyACMx, please kill that process and try '
                       'again.')

    self.parse_output(dut_results, th_results)
    pretty_results = self.prettify_results()
    html_results = self.results_as_html()

    dest = os.path.join(self.results_dir, self.dut.board, self.module + '.html')
    if not os.path.exists(os.path.dirname(dest)):
      os.makedirs(os.path.dirname(dest))

    with open(dest, 'w') as fl:
      fl.write(html_results)

    print pretty_results


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
  parser.add_argument('--debug',
                      action='store_true',
                      help=('If building, build with debug printing enabled. '
                            'This may change test results'))
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

  cts = Cts(ec_dir, dut=dut, module=module, debug=args.debug)

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
