# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from abc import ABCMeta, abstractmethod
import fcntl
import os
import select
import subprocess as sp
import time


OCD_SCRIPT_DIR = '/usr/local/share/openocd/scripts'
OPENOCD_CONFIGS = {
    'stm32l476g-eval': 'board/stm32l4discovery.cfg',
    'nucleo-f072rb': 'board/st_nucleo_f0.cfg',
}
FLASH_OFFSETS = {
    'stm32l476g-eval': '0x08000000',
    'nucleo-f072rb': '0x08000000',
}


class Board(object):
  """Class representing a single board connected to a host machine

  Attributes:
    board: String containing actual type of board, i.e. nucleo-f072rb
    config: Directory of board config file relative to openocd's
        scripts directory
    hla_serial: String containing board's hla_serial number (if board
    is an stm32 board)
    tty_port: String that is the path to the tty port which board's
        UART outputs to
    tty: String of file descriptor for tty_port
  """

  __metaclass__ = ABCMeta  # This is an Abstract Base Class (ABC)
  def __init__(self, board, hla_serial=None):
    """Initializes a board object with given attributes

    Args:
      board: String containing board name
      hla_serial: Serial number if board's adaptor is an HLA
    """
    if not board in OPENOCD_CONFIGS:
      msg = 'OpenOcd configuration not found for ' + board
      raise RuntimeError(msg)
    self.openocd_config = OPENOCD_CONFIGS[board]
    if not board in FLASH_OFFSETS:
      msg = 'Flash offset not found for ' + board
      raise RuntimeError(msg)
    self.flash_offset = FLASH_OFFSETS[board]
    self.board = board
    self.hla_serial = hla_serial
    self.tty_port = None
    self.tty = None

  @staticmethod
  def get_stlink_serials():
    """Gets serial numbers of all st-link v2.1 board attached to host

    Returns:
      List of serials
    """
    usb_args = ['lsusb', '-v', '-d', '0x0483:0x374b']
    usb_process = sp.Popen(usb_args, stdout=sp.PIPE, shell=False)
    st_link_info = usb_process.communicate()[0]
    st_serials = []
    for line in st_link_info.split('\n'):
      if not 'iSerial' in line:
        continue
      words = line.split()
      if len(words) <= 2:
        continue
      st_serials.append(words[2].strip())
    return st_serials

  @abstractmethod
  def get_serial(self):
    """Subclass should implement this"""
    pass

  def send_open_ocd_commands(self, commands):
    """Send a command to the board via openocd

    Args:
      commands: A list of commands to send
    """
    args = ['openocd', '-s', OCD_SCRIPT_DIR,
            '-f', self.openocd_config, '-c', 'hla_serial ' + self.hla_serial]

    for cmd in commands:
      args += ['-c', cmd]
    args += ['-c', 'shutdown']
    return sp.call(args)

  def build(self, module, ec_dir, debug=False):
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
            '-j']

    if debug:
      cmds.append('CTS_DEBUG=TRUE')

    print ' '.join(cmds)
    return sp.call(cmds)

  def flash(self, image_path):
    """Flashes board with most recent build ec.bin"""
    cmd = ['reset_config connect_assert_srst',
           'init',
           'reset init',
           'flash write_image erase %s %s' % (image_path, self.flash_offset)]
    return self.send_open_ocd_commands(cmd)

  def to_string(self):
    s = ('Type: Board\n'
         'board: ' + self.board + '\n'
         'hla_serial: ' + self.hla_serial + '\n'
         'openocd_config: ' + self.openocd_config + '\n'
         'tty_port: ' + self.tty_port + '\n'
         'tty: ' + str(self.tty) + '\n')
    return s

  def reset(self):
    """Reset board (used when can't connect to TTY)"""
    self.send_open_ocd_commands(['init', 'reset init', 'resume'])

  def setup_tty(self):
    """Call this before trying to call readOutput for the first time.
    This is not in the initialization because caller only should call
    this function after serial numbers are setup
    """
    self.get_serial()
    self.reset()
    self.identify_tty_port()

    # In testing 3 retries is enough to reset board (2 can fail)
    num_file_setup_retries = 3
    # In testing, 10 seconds is sufficient to allow board to reconnect
    reset_wait_time_seconds = 10
    tty = None
    try:
      tty = self.open_tty()
    # If board was just connected, must be reset to be read from
    except (IOError, OSError):
      for i in range(num_file_setup_retries):
        self.reset()
        time.sleep(reset_wait_time_seconds)
        try:
          tty = self.open_tty()
          break
        except (IOError, OSError):
          continue
    if not tty:
      raise ValueError('Unable to read ' + self.board + '. If you are running '
                       'cat on a ttyACMx file, please kill that process and '
                       'try again')
    self.tty = tty

  def read_tty(self):
    """Read info from a serial port described by a file descriptor

    Return:
      Bytes that UART has output
    """
    buf = []
    while True:
      if select.select([self.tty], [], [], 1)[0]:
        buf.append(os.read(self.tty, 1))
      else:
        break
    result = ''.join(buf)
    return result

  def identify_tty_port(self):
    """Saves this board's serial port"""
    dev_dir = '/dev'
    id_prefix = 'ID_SERIAL_SHORT='
    num_reset_tries = 3
    reset_wait_time_s = 10
    com_devices = [f for f in os.listdir(dev_dir) if f.startswith('ttyACM')]

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
      if i != num_reset_tries - 1: # No need to reset the board the last time
        self.reset() # May need to reset to connect
        time.sleep(reset_wait_time_s)

    # If we get here without returning, something is wrong
    raise RuntimeError('The device dev path could not be found')

  def open_tty(self):
    """Read available bytes from device dev path"""
    fd = os.open(self.tty_port, os.O_RDONLY)
    flag = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flag | os.O_NONBLOCK)
    return fd


class TestHarness(Board):
  """Subclass of Board representing a Test Harness

  Attributes:
    serial_path: Path to file containing serial number
  """

  def __init__(self, board, serial_path):
    """Initializes a board object with given attributes

    Args:
      serial_path: Path to file containing serial number
    """
    Board.__init__(self, board=board)
    self.serial_path = serial_path

  def get_serial(self):
    """Loads serial number from saved location"""
    if self.hla_serial:
      return # serial was already loaded
    try:
      with open(self.serial_path, mode='r') as ser_f:
        self.hla_serial = ser_f.read()
        return
    except IOError:
      msg = ('Your TH board has not been identified.\n'
             'Connect only TH and run the script --setup, then try again.')
      raise RuntimeError(msg)

  def save_serial(self):
    """Saves the TH serial number to a file"""
    serials = Board.get_stlink_serials()
    if len(serials) > 1:
      msg = ('There are more than one test board connected to the host.'
             '\nConnect only the test harness and remove other boards.')
      raise RuntimeError(msg)
    if len(serials) < 1:
      msg = ('No test boards were found.'
             '\nTry to run the script outside chroot.')
      raise RuntimeError(msg)

    serial = serials[0]
    dir = os.path.dirname(self.serial_path)
    if not os.path.exists(dir):
      os.makedirs(dir)
    with open(self.serial_path, mode='w') as ser_f:
      ser_f.write(serial)
      self.hla_serial = serial

    print 'Your TH serial', serial, 'has been saved as', self.serial_path
    return


class DeviceUnderTest(Board):
  """Subclass of Board representing a DUT board

  Attributes:
    th: Reference to test harness board to which this DUT is attached
  """

  def __init__(self, board, th, hla_ser=None):
    """Initializes a Device Under Test object with given attributes

    Args:
      board: String containing board name
      th: Reference to test harness board to which this DUT is attached
      hla_serial: Serial number if board uses an HLA adaptor
    """
    Board.__init__(self, board, hla_serial=hla_ser)
    self.th = th

  def get_serial(self):
    """Get serial number.

    Precondition: The DUT and TH must both be connected, and th.hla_serial
    must hold the correct value (the th's serial #)
    """
    if self.hla_serial != None:
      # serial was already set ('' is a valid serial)
      return

    serials = Board.get_stlink_serials()
    dut = [s for s in serials if self.th.hla_serial != s]

    # If len(dut) is 0 then your dut doesn't use an st-link device, so we
    # don't have to worry about its serial number
    if  len(dut) != 1:
      msg = ('Your TH serial number is incorrect, or your have'
             ' too many st-link devices attached.')
      raise RuntimeError(msg)

    # Found your other st-link device serial!
    self.hla_serial = dut[0]
    return