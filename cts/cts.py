#!/usr/bin/python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is a utility to quickly flash boards

import argparse
import collections
import fcntl
import os
import select
import subprocess as sp
import time
from copy import deepcopy
from abc import ABCMeta, abstractmethod
import xml.etree.ElementTree as et

CTS_CORRUPTED_CODE = -2  # The test didn't execute correctly
CTS_CONFLICTING_CODE = -1 # Error codes should never conflict
CTS_SUCCESS_CODE = 0
CTS_COLOR_RED = '#fb7d7d'
CTS_COLOR_GREEN = '#7dfb9f'
TH_BOARD = 'stm32l476g-eval'
OCD_SCRIPT_DIR = '/usr/local/share/openocd/scripts'
MAX_SUITE_TIME_SEC = 3
CTS_DEBUG_START = '[DEBUG]'
CTS_DEBUG_END = '[DEBUG_END]'

class Board(object):
  """Class representing a single board connected to a host machine

  This class is abstract, subclasses must define the updateSerial()
  method

  Attributes:
    board: String containing actual type of board, i.e. nucleo-f072rb
    config: Directory of board config file relative to openocd's
        scripts directory
    hla_serial: String containing board's hla_serial number (if board
    is an stm32 board)
    tty_port: String that is the path to the tty port which board's
        UART outputs to
    _tty_descriptor: String of file descriptor for tty_port
  """

  configs = {
      'stm32l476g-eval': 'board/stm32l4discovery.cfg',
      'nucleo-f072rb': 'board/st_nucleo_f0.cfg'
  }

  __metaclass__ = ABCMeta  # This is an Abstract Base Class (ABC)
  def __init__(self, board, hla_serial=None, flash_offset='0x08000000'):
    """Initializes a board object with given attributes

    Args:
      board: String containing board name
      hla_serial: Serial number if board's adaptor is an HLA
    """
    self.board = board
    self.hla_serial = hla_serial
    self.tty_port = None
    self._tty_descriptor = None
    self.flash_offset = flash_offset

  @abstractmethod
  def updateSerial(self):
    """Subclass should implement this"""
    pass

  def sendOpenOcdCommands(self, commands):
    """Send a command to the board via openocd

    Args:
      commands: A list of commands to send
    """
    args = ['openocd', '-s', OCD_SCRIPT_DIR,
        '-f', Board.configs[self.board], '-c', 'hla_serial ' + self.hla_serial]

    for cmd in commands:
      args += ['-c', cmd]
    args += ['-c', 'shutdown']
    sp.call(args)

  def make(self, module, ec_dir, debug=False):
    """Builds test suite module for board

    Args:
      module: String of the test module you are building,
        i.e. gpio, timer, etc.
      ec_dir: String of the ec directory path
      debug: True means compile in debug messages when building (may
        affect test results)
    """
    cmds = ['make',
        '--directory=' + ec_dir,
        'BOARD=' + self.board,
        'CTS_MODULE=' + module,
        '-j',
        '-B']

    if debug:
      cmds.append('CTS_DEBUG=TRUE')

    print 'EC directory is ' + ec_dir
    print (
        'Building module \'' + module + '\' for ' + self.board +
        'with debug = ' + str(debug))
    sp.call(cmds)

  def flash(self):
    """Flashes board with most recent build ec.bin"""
    flash_cmds = [
        'reset_config connect_assert_srst',
        'init',
        'reset init',
        'flash write_image erase build/' +
            self.board +
            '/ec.bin ' +
            self.flash_offset,
        'reset']

    self.sendOpenOcdCommands(flash_cmds)

  def toString(self):
    s = ('Type: Board\n'
       'board: ' + self.board + '\n'
       'hla_serial: ' + self.hla_serial + '\n'
       'config: ' + Board.configs[self.board] + '\n'
       'tty_port ' + self.tty_port + '\n'
       '_tty_descriptor: ' + str(self._tty_descriptor) + '\n')
    return s

  def reset(self):
    """Reset board (used when can't connect to TTY)"""
    self.sendOpenOcdCommands(['init', 'reset init', 'resume'])

  def setupForOutput(self):
    """Call this before trying to call readOutput for the first time.
    This is not in the initialization because caller only should call
    this function after serial numbers are setup
    """
    self.updateSerial()
    self.reset()
    self._identifyTtyPort()

    # In testing 3 retries is enough to reset board (2 can fail)
    num_file_setup_retries = 3
    # In testing, 10 seconds is sufficient to allow board to reconnect
    reset_wait_time_seconds = 10
    try:
      self._getDevFileDescriptor()
    # If board was just connected, must be reset to be read from
    except (IOError, OSError):
      for i in range(num_file_setup_retries):
        self.reset()
        time.sleep(reset_wait_time_seconds)
        try:
          self._getDevFileDescriptor()
          break
        except (IOError, OSError):
          continue
    if self._tty_descriptor is None:
      raise ValueError('Unable to read ' + self.name + '\n'
               'If you are running cat on a ttyACMx file,\n'
               'please kill that process and try again')

  def readAvailableBytes(self):
    """Read info from a serial port described by a file descriptor

    Return:
      Bytes that UART has output
    """
    buf = []
    while True:
      if select.select([self._tty_descriptor], [], [], 1)[0]:
        buf.append(os.read(self._tty_descriptor, 1))
      else:
        break
    result = ''.join(buf)
    return result

  def _identifyTtyPort(self):
    """Saves this board's serial port"""
    dev_dir = '/dev/'
    id_prefix = 'ID_SERIAL_SHORT='
    num_reset_tries = 3
    reset_wait_time_s = 10
    com_devices = [f for f in os.listdir(
      dev_dir) if f.startswith('ttyACM')]

    for i in range(num_reset_tries):
      for device in com_devices:
        self.tty_port = os.path.join(dev_dir, device)
        properties = sp.check_output(['udevadm',
                        'info',
                        '-a',
                        '-n',
                        self.tty_port,
                        '--query=property'])
        for line in [l.strip() for l in properties.split('\n')]:
          if line.startswith(id_prefix):
            if self.hla_serial == line[len(id_prefix):]:
              return
      if i != num_reset_tries - 1: # No need to reset the obard the last time
        self.reset() # May need to reset to connect
        time.sleep(reset_wait_time_s)

    # If we get here without returning, something is wrong
    raise RuntimeError('The device dev path could not be found')

  def _getDevFileDescriptor(self):
    """Read available bytes from device dev path"""
    fd = os.open(self.tty_port, os.O_RDONLY)
    flag = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flag | os.O_NONBLOCK)
    self._tty_descriptor = fd

class TestHarness(Board):
  """Subclass of Board representing a Test Harness

  Attributes:
    serial_path: Path to file containing serial number
  """

  def __init__(self, serial_path=None):
    """Initializes a board object with given attributes

    Args:
      serial_path: Path to file containing serial number
    """
    Board.__init__(self, TH_BOARD)
    self.serial_path = serial_path

  def updateSerial(self):
    """Loads serial number from saved location"""
    if self.hla_serial:
      return # serial was already loaded
    try:
      with open(self.serial_path, mode='r') as ser_f:
        self.hla_serial = ser_f.read()
        return
    except IOError:
      msg = ('Your th hla_serial may not have been saved.\n'
           'Connect only your th and run ./cts --setup, then try again.')
      raise RuntimeError(msg)

  def saveSerial(self):
    """Saves the th serial number to a file

    Return: the serial number saved
    """
    serial = Cts.getSerialNumbers()
    if len(serial) != 1:
      msg = ('TH could not be identified.\n'
           '\nConnect your TH and remove other st-link devices')
      raise RuntimeError(msg)
    else:
      ser = serial[0]
      if not ser:
        msg = ('Unable to save serial')
        raise RuntimeError(msg)
      if not os.path.exists(os.path.dirname(self.serial_path)):
        os.makedirs(os.path.dirname(self.serial_path))
      with open(self.serial_path, mode='w') as ser_f:
        ser_f.write(ser)
        self.hla_serial = ser
        return ser

class DeviceUnderTest(Board):
  """Subclass of Board representing a DUT board

  Attributes:
    th: Reference to test harness board to which this DUT is attached
  """

  def __init__(self, board, th, hla_ser=None, f_offset='0x08000000'):
    """Initializes a Device Under Test object with given attributes

    Args:
      board: String containing board name
      th: Reference to test harness board to which this DUT is attached
      hla_serial: Serial number if board uses an HLA adaptor
    """
    Board.__init__(self, board, hla_serial=hla_ser, flash_offset=f_offset)
    self.th = th

  def updateSerial(self):
    """Stores the DUT's serial number.

    Precondition: The DUT and TH must both be connected, and th.hla_serial
    must hold the correct value (the th's serial #)
    """
    if self.hla_serial != None:
      return # serial was already set ('' is a valid serial)
    serials = Cts.getSerialNumbers()
    dut = [s for s in serials if self.th.hla_serial != s]
    if  len(dut) == 1:
      self.hla_serial = dut[0]
      return # Found your other st-link device serial!
    else:
      raise RuntimeError('Your TH serial number is incorrect, or your have'
          ' too many st-link devices attached.')
    # If len(dut) is 0 then your dut doesn't use an st-link device, so we
    # don't have to worry about its serial number

class Cts(object):
  """Class that represents a CTS testing setup and provides
  interface to boards (building, flashing, etc.)

  Attributes:
    dut: DeviceUnderTest object representing dut
    th: TestHarness object representing th
    module: Name of module to build/run tests for
    ec_directory: String containing path to EC top level directory
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

  def __init__(self, ec_dir,
               dut='nucleo-f072rb', module='gpio', debug=False):
    """Initializes cts class object with given arguments.

    Args:
      dut: Name of Device Under Test (DUT) board
      ec_dir: String path to ec directory
      dut: Name of board to use for DUT
      module: Name of module to build/run tests for
      debug: Boolean that indicates whether or not on-board debug message
        printing should be enabled when building.
    """
    self.results_dir = '/tmp/cts_results'
    self.module = module
    self.debug = debug
    self.ec_directory = ec_dir
    self.th = TestHarness()
    self.dut = DeviceUnderTest(dut, self.th)  # DUT constructor needs TH

    th_ser_path = os.path.join(
        self.ec_directory,
        'build',
        self.th.board,
        'th_hla_serial')

    self.module = module

    testlist_path = os.path.join(
        self.ec_directory,
        'cts',
        self.module,
        'cts.testlist')

    self.test_names = Cts._getMacroArgs(testlist_path, 'CTS_TEST')

    self.debug_output = {}
    for test in self.test_names:
      self.debug_output[test] = []

    self.th.serial_path = th_ser_path


    return_codes_path = os.path.join(self.ec_directory,
        'cts',
        'common',
        'cts.rc')

    self.return_codes = dict(enumerate(Cts._getMacroArgs(
        return_codes_path, 'CTS_RC_')))

    self.return_codes[CTS_CONFLICTING_CODE] = 'RESULTS CONFLICT'
    self.return_codes[CTS_CORRUPTED_CODE] = 'CORRUPTED'

    self.test_results = collections.OrderedDict()

  def make(self):
    self.dut.make(self.module, self.ec_directory, self.debug)
    self.th.make(self.module, self.ec_directory, self.debug)

  def flashBoards(self):
    """Flashes th and dut boards with their most recently build ec.bin"""
    self.updateSerials()
    self.th.flash()
    self.dut.flash()

  def setup(self):
    """Saves th serial number if th only is connected.

    Return:
      Serial number that was saved
    """
    return self.th.saveSerial()

  def updateSerials(self):
    """Updates serials of both th and dut, in that order (order matters)"""
    self.th.updateSerial()
    self.dut.updateSerial()

  def resetBoards(self):
    """Resets the boards and allows them to run tests
    Due to current (7/27/16) version of sync function,
    both boards must be rest and halted, with the th
    resuming first, in order for the test suite to run
    in sync
    """
    self.updateSerials()
    self.th.sendOpenOcdCommands(['init', 'reset halt'])
    self.dut.sendOpenOcdCommands(['init', 'reset halt'])
    self.th.sendOpenOcdCommands(['init', 'resume'])
    self.dut.sendOpenOcdCommands(['init', 'resume'])

  @staticmethod
  def getSerialNumbers():
    """Gets serial numbers of all st-link v2.1 board attached to host

    Returns:
      List of serials
    """
    usb_args = ['lsusb', '-v', '-d', '0x0483:0x374b']
    usb_process = sp.Popen(usb_args, stdout=sp.PIPE, shell=False)
    st_link_info = usb_process.communicate()[0]
    st_serials = []
    for line in st_link_info.split('\n'):
      if 'iSerial' in line:
        st_serials.append(line.split()[2].strip())
    return st_serials

  @staticmethod
  def _getMacroArgs(filepath, macro):
    """Get list of args of a certain macro in a file when macro is used
    by itself on a line

    Args:
      filepath: String containing absolute path to the file
      macro: String containing text of macro to get args of
    """
    args = []
    with open(filepath, 'r') as fl:
      for ln in [ln for ln in fl.readlines(
      ) if ln.strip().startswith(macro)]:
        ln = ln.strip()[len(macro):]
        args.append(ln.strip('()').replace(',', ''))
    return args

  def extractDebugOutput(self, output):
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

  def _parseOutput(self, r1, r2):
    """Parse the outputs of the DUT and TH together

    Args;
      r1: String output of one of the DUT or the TH (order does not matter)
      r2: String output of one of the DUT or the TH (order does not matter)
    """
    self.test_results.clear()  # empty out any old results

    first_corrupted_test = len(self.test_names)

    self.extractDebugOutput(r1)
    self.extractDebugOutput(r2)

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
        elif return_code != self.test_results[test]:
          self.test_results[test] = CTS_CONFLICTING_CODE
        test_num += 1

      if test_num != len(self.test_names): # If a suite didn't finish
        first_corrupted_test = min(first_corrupted_test, test_num)

    if first_corrupted_test < len(self.test_names):
      for test in self.test_names[first_corrupted_test:]:
        self.test_results[test] = CTS_CORRUPTED_CODE

  def _resultsAsString(self):
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

  def prettyResults(self):
    """Takes saved results and returns a string representation of them

    Return: Dictionary similar to self.test_results, but with strings
        instead of error codes
    """
    res = self._resultsAsString()
    t_long = max(len(s) for s in res.keys())
    e_max_len = max(len(s) for s in res.values())

    pretty_results = 'CTS Test Results for ' + self.module + ' module:\n'

    for test, code in res.items():
      align_str = '\n{0:<' + str(t_long) + \
        '} {1:>' + str(e_max_len) + '}'
      pretty_results += align_str.format(test, code)

    return pretty_results

  def resultsAsHtml(self):
    res = self._resultsAsString()
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

  def resetAndRecord(self):
    """Resets boards, records test results in results dir"""
    self.updateSerials()
    self.dut.setupForOutput()
    self.th.setupForOutput()

    self.dut.readAvailableBytes()  # clear buffer
    self.th.readAvailableBytes()
    bad_cat_message = (
        'Output missing from boards.\n'
        'If you are running cat on a ttyACMx file,\n'
        'please kill that process and try again'
        )

    self.resetBoards()

    time.sleep(MAX_SUITE_TIME_SEC)

    dut_results = self.dut.readAvailableBytes()
    th_results = self.th.readAvailableBytes()

    if not dut_results or not th_results:
      raise ValueError(bad_cat_message)

    self._parseOutput(dut_results, th_results)
    pretty_results = self.prettyResults()
    html_results = self.resultsAsHtml()

    dest = os.path.join(
        self.results_dir,
        self.dut.board,
        self.module + '.html'
        )
    if not os.path.exists(os.path.dirname(dest)):
      os.makedirs(os.path.dirname(dest))

    with open(dest, 'w') as fl:
      fl.write(html_results)

    print pretty_results

def main():
  """Main entry point for cts script from command line"""
  ec_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
  os.chdir(ec_dir)

  dut_board = 'nucleo-f072rb'  # nucleo by default
  module = 'gpio'  # gpio by default
  debug = False

  parser = argparse.ArgumentParser(description='Used to build/flash boards')
  parser.add_argument('-d',
            '--dut',
            help='Specify DUT you want to build/flash')
  parser.add_argument('-m',
            '--module',
            help='Specify module you want to build/flash')
  parser.add_argument('--debug',
            action='store_true',
            help='If building, build with debug printing enabled. This may'
                 'change test results')
  parser.add_argument('-s',
            '--setup',
            action='store_true',
            help='Connect only the th to save its serial')
  parser.add_argument('-b',
            '--build',
            action='store_true',
            help='Build test suite (no flashing)')
  parser.add_argument('-f',
            '--flash',
            action='store_true',
            help='Flash boards with most recent image and record results')
  parser.add_argument('-r',
            '--reset',
            action='store_true',
            help='Reset boards and save test results (no flashing)')

  args = parser.parse_args()

  if args.module:
    module = args.module

  if args.dut:
    dut_board = args.dut

  if args.debug:
    debug = args.debug

  cts_suite = Cts(ec_dir, module=module, dut=dut_board, debug=debug)

  if args.setup:
    serial = cts_suite.setup()
    print 'Your th hla_serial # has been saved as: ' + serial

  elif args.reset:
    cts_suite.resetAndRecord()

  elif args.build:
    cts_suite.make()

  elif args.flash:
    cts_suite.flashBoards()

  else:
    cts_suite.make()
    cts_suite.flashBoards()

if __name__ == "__main__":
  main()
