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

# For most tests, error codes should never conflict
CTS_CONFLICTING_CODE = -1
CTS_SUCCESS_CODE = 0


class Cts(object):
  """Class that represents a CTS testing setup and provides
  interface to boards (building, flashing, etc.)

  Attributes:
    ocd_script_dir: String containing locations of openocd's config files
    th_board: String containing name of the Test Harness (th) board
    results_dir: String containing test output directory path
    dut_board: Name of Device Under Test (DUT) board
    module: Name of module to build/run tests for
    ec_directory: String containing path to EC top level directory
    th_hla: String containing hla_serial for the th
    dut_hla: String containing hla_serial for the dut, only used for
    boards which have an st-link v2.1 debugger
    th_ser_path: String which contains full path to th serial file
    test_names: List of strings of test names contained in given module
    test_results: Dictionary of results of each test from module
    return_codes: List of strings of return codes, with a code's integer
    value being the index for the corresponding string representation
  """

  def __init__(self, ec_dir, dut_board='nucleo-f072rb', module='gpio'):
    """Initializes cts class object with given arguments.

    Args:
      dut_board: Name of Device Under Test (DUT) board
      module: Name of module to build/run tests for
    """
    self.ocd_script_dir = '/usr/local/share/openocd/scripts'
    self.th_board = 'stm32l476g-eval'
    self.results_dir = '/tmp/cts_results'
    self.dut_board = dut_board
    self.module = module
    self.ec_directory = ec_dir
    self.th_hla = ''
    self.dut_hla = ''
    self.th_ser_path = os.path.join(
      self.ec_directory,
      'build',
      self.th_board,
      'th_hla_serial')
    testlist_path = os.path.join(
      self.ec_directory,
      'cts',
      self.module,
      'cts.testlist')
    self.test_names = self.getMacroArgs(testlist_path, 'CTS_TEST')
    return_codes_path = os.path.join(self.ec_directory,
                    'cts',
                    'common',
                    'cts.rc')
    self.return_codes = self.getMacroArgs(
      return_codes_path, 'CTS_RC_')
    self.test_results = collections.OrderedDict()

  def set_dut_board(self, brd):
    """Sets the dut_board instance variable

    Args:
      brd: String of board name
    """
    self.dut_board = brd

  def set_module(self, mod):
    """Sets the module instance variable

    Args:
      brd: String of board name
    """
    self.module = mod

  def make(self):
    """Builds test suite module for given th/dut boards"""
    print 'Building module \'' + self.module + '\' for th ' + self.th_board
    sp.call(['make',
         '--directory=' + str(self.ec_directory),
         'BOARD=' + self.th_board,
         'CTS_MODULE=' + self.module,
         '-j'])

    print 'Building module \'' + self.module + '\' for dut ' + self.dut_board
    sp.call(['make',
         '--directory=' + str(self.ec_directory),
         'BOARD=' + self.dut_board,
         'CTS_MODULE=' + self.module,
         '-j'])

  def openocdCmd(self, command_list, board):
    """Sends the specified commands to openocd for a board

    Args:
      board: String that contains board name
    """

    board_cfg = self.getBoardConfigName(board)

    args = ['openocd', '-s', self.ocd_script_dir,
        '-f', board_cfg]
    for cmd in command_list:
      args.append('-c')
      args.append(cmd)
    args.append('-c')
    args.append('shutdown')
    sp.call(args)

  def getStLinkSerialNumbers(self):
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
        st_serials.append(line.split()[2])
    return st_serials

  # params: th_hla_serial is your personal th board's serial
  def saveDutSerial(self):
    """If dut uses same debugger as th, save its serial"""
    stlink_serials = self.getStLinkSerialNumbers()
    if len(stlink_serials) == 1:  # dut doesn't use same debugger
      return ''
    elif len(stlink_serials) == 2:
      dut = [s for s in stlink_serials if self.th_hla not in s]
      if len(dut) != 1:
        raise RuntimeError('Incorrect TH hla_serial')
      else:
        return dut[0]  # Found your other st-link device serial!
    else:
      msg = ('Please connect TH and your DUT\n'
           'and remove all other st-link devices')
      raise RuntimeError(msg)

  def saveThSerial(self):
    """Saves the th serial number to a file located at th_ser_path

    Return: the serial number saved
    """
    serial = self.getStLinkSerialNumbers()
    if len(serial) != 1:
      msg = ('TH could not be identified.\n'
           '\nConnect your TH and remove other st-link devices')
      raise RuntimeError(msg)
    else:
      ser = serial[0]
      if not os.path.exists(os.path.dirname(self.th_ser_path)):
        os.makedirs(os.path.dirname(self.th_ser_path))
      with open(self.th_ser_path, mode='w') as ser_f:
        ser_f.write(ser)
      return ser

  def getBoardConfigName(self, board):
    """Gets the path for the config file relative to the
    openocd scripts directory

    Args:
      board: String containing name of board to get the config file for

    Returns: String containing relative path to board config file
    """
    board_config_locs = {
      'stm32l476g-eval': 'board/stm32l4discovery.cfg',
      'nucleo-f072rb': 'board/st_nucleo_f0.cfg'
    }

    try:
      cfg = board_config_locs[board]
      return cfg
    except KeyError:
      raise ValueError(
        'The config file for board ' +
        board +
        ' was not found')

  def flashBoards(self):
    """Flashes th and dut boards with their most recently build ec.bin"""
    self.updateSerials()
    th_flash_cmds = [
      'hla_serial ' +
      self.th_hla,
      'reset_config connect_assert_srst',
      'init',
      'reset init',
      'flash write_image erase build/' +
      self.th_board +
      '/ec.bin 0x08000000',
      'reset halt']

    dut_flash_cmds = [
      'hla_serial ' +
      self.dut_hla,
      'reset_config connect_assert_srst',
      'init',
      'reset init',
      'flash write_image erase build/' +
      self.dut_board +
      '/ec.bin 0x08000000',
      'reset halt']

    self.openocdCmd(th_flash_cmds, self.th_board)
    self.openocdCmd(dut_flash_cmds, self.dut_board)
    self.openocdCmd(['hla_serial ' + self.th_hla,
             'init',
             'reset init',
             'resume'],
              self.th_board)
    self.openocdCmd(['hla_serial ' + self.dut_hla,
             'init',
             'reset init',
             'resume'],
             self.dut_board)

  def updateSerials(self):
    """Updates serial #s for th and dut"""
    try:
      with open(self.th_ser_path) as th_f:
        self.th_hla = th_f.read()
    except IOError:
      msg = ('Your th hla_serial may not have been saved.\n'
             'Connect only your th and run ./cts --setup, then try again.')
      raise RuntimeError(msg)
    self.saveDutSerial()

  def resetBoards(self):
    """Resets the boards and allows them to run tests"""
    self.updateSerials()
    self.openocdCmd(['hla_serial ' + self.dut_hla,
             'init', 'reset init'], self.dut_board)
    self.openocdCmd(['hla_serial ' + self.th_hla,
             'init', 'reset init'], self.th_board)
    self.openocdCmd(['hla_serial ' + self.th_hla,
             'init', 'resume'], self.th_board)
    self.openocdCmd(['hla_serial ' + self.dut_hla,
             'init', 'resume'], self.dut_board)

  def readAvailableBytes(self, fd):
    """Read info from a serial port described by a file descriptor

    Args:
      fd: file descriptor for device ttyACM file
    """
    buf = []
    while True:
      if select.select([fd], [], [], 1)[0]:
        buf.append(os.read(fd, 1))
      else:
        break
    result = ''.join(buf)
    return result

  def getDevFileDescriptor(self, path):
    """Read available bytes from device dev path

    Args:
      path: The serial device file path to read from

    Return: the file descriptor for the open serial device file
    """
    fd = os.open(path, os.O_RDONLY)
    flag = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flag | os.O_NONBLOCK)
    return fd

  def getDevFilenames(self):
    """Read available bytes from device dev path

    Args:
      path: The serial device file path to read from

    Return: the file descriptor for the open serial device file
    """
    com_files = [f for f in os.listdir('/dev/') if f.startswith('ttyACM')]
    if len(com_files) < 2:
      raise RuntimeError('The device dev paths could not be found')
    elif len(com_files) > 2:
      raise RuntimeError('Too many serial devices connected to host')
    else:
      return ('/dev/' + com_files[0], '/dev/' + com_files[1])

  def getMacroArgs(self, filepath, macro):
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
        args.append(ln.strip('()').replace(',',''))
    return args

  def parseOutput(self, r1, r2):
    """Parse the outputs of the DUT and TH together

    Args;
      r1: String output of one of the DUT or the TH (order does not matter)
      r2: String output of one of the DUT or the TH (order does not matter)
    """
    self.test_results.clear()  # empty out any old results

    for output_str in [r1, r2]:
      for ln in [ln.strip() for ln in output_str.split('\n')]:
        tokens = ln.split()
        if len(tokens) != 2:
          continue
        elif tokens[0].strip() not in self.test_names:
          continue
        elif tokens[0] in self.test_results.keys():
          if self.test_results[tokens[0]] != int(tokens[1]):
            if self.test_results[tokens[0]] == CTS_SUCCESS_CODE:
              self.test_results[tokens[0]] = int(tokens[1])
            elif int(tokens[1]) == CTS_SUCCESS_CODE:
              continue
            else:
              self.test_results[tokens[0]] = CTS_CONFLICTING_CODE
          else:
            continue
        else:
          self.test_results[tokens[0]] = int(tokens[1])

    # Convert codes to strings
    for test, code in self.test_results.items():
      if code == CTS_CONFLICTING_CODE:
        self.test_results[test] = 'RESULTS CONFLICT'
      self.test_results[test] = self.return_codes[code]

    for tn in self.test_names:
      if tn not in self.test_results.keys():
        self.test_results[tn] = 'NO RESULT RETURNED' # Exceptional case

  def resultsAsString(self):
    """Takes saved results and returns a string representation of them

    Return: Saved string that contains results
    """
    t_long = max(len(s) for s in self.test_results.keys())
    e_max_len = max(len(s) for s in self.test_results.values())

    pretty_results = 'CTS Test Results for ' + self.module + ' module:\n'

    for test, code in self.test_results.items():
      align_str = '\n{0:<' + str(t_long) + \
        '} {1:>' + str(e_max_len) + '}'
      pretty_results += align_str.format(test, code)

    return pretty_results

  def resetAndRecord(self):
    """Resets boards, records test results in results dir"""

    self.resetBoards()
    # Doesn't matter which is dut or th because we combine their results
    d1, d2 = self.getDevFilenames()

    try:
      fd1 = self.getDevFileDescriptor(d1)
      fd2 = self.getDevFileDescriptor(d2)
    except:  # If board was just connected, must be reset to be read from
      for i in range(3):
        self.resetBoards()
        time.sleep(10)
        try:
          fd1 = self.getDevFileDescriptor(d1)
          fd2 = self.getDevFileDescriptor(d2)
          break
        except:
          continue

    self.readAvailableBytes(fd1)  # clear any junk from buffer
    self.readAvailableBytes(fd2)
    self.resetBoards()
    time.sleep(3)
    res1 = self.readAvailableBytes(fd1)
    res2 = self.readAvailableBytes(fd2)
    if len(res1) == 0 or len(res2) == 0:
      raise ValueError('Output missing from boards.\n'
                       'If you are running cat on a ttyACMx file,\n'
                       'please kill that process and try again')
    self.parseOutput(res1, res2)
    pretty_results = self.resultsAsString()

    dest = os.path.join(
      self.results_dir,
      self.dut_board,
      self.module + '.txt')
    if not os.path.exists(os.path.dirname(dest)):
      os.makedirs(os.path.dirname(dest))

    with open(dest, 'w') as fl:
      fl.write(pretty_results)

    print pretty_results

def main():
  """Main entry point for cts script from command line"""
  ec_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
  os.chdir(ec_dir)

  cts_suite = Cts(ec_dir)
  dut_board = 'nucleo-f072rb'  # nucleo by default
  module = 'gpio'  # gpio by default

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
            help='Connect only the th to save its serial')
  parser.add_argument('-b',
            '--build',
            action='store_true',
            help='Build test suite (no flashing)')
  parser.add_argument('-f',
            '--flash',
            action='store_true',
            help='Flash boards with last image built for them')
  parser.add_argument('-r',
            '--reset',
            action='store_true',
            help='Reset boards and save test results')

  args = parser.parse_args()

  if args.module:
    module = args.module
    cts_suite.set_module(module)

  if args.dut:
    dut_board = args.dut
    cts_suite.set_dut_board(dut_board)

  if args.setup:
    serial = cts_suite.saveThSerial()
    if(serial is not None):
      print 'Your th hla_serial # has been saved as: ' + serial
    else:
      print 'Unable to save serial'
    return

  if args.reset:
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
