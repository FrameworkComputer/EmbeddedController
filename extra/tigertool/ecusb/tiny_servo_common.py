# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities for using lightweight console functions."""

# Note: This is a py2/3 compatible file.

import datetime
import errno
import os
import re
import subprocess
import sys
import time

import six

from . import pty_driver
from . import stm32uart


def get_subprocess_args():
  if six.PY3:
    return {'encoding': 'utf-8'}
  return {}


class TinyServoError(Exception):
  """Exceptions."""


def log(output):
  """Print output to console, logfiles can be added here.

  Args:
    output: string to output.
  """
  sys.stdout.write(output)
  sys.stdout.write('\n')
  sys.stdout.flush()

def check_usb(vidpid, serialname=None):
  """Check if |vidpid| is present on the system's USB.

  Args:
    vidpid: string representation of the usb vid:pid, eg. '18d1:2001'
    serialname: serialname if specified.

  Returns: True if found, False, otherwise.
  """
  if serialname:
    output = subprocess.check_output(['lsusb', '-v', '-d', vidpid],
                                     **get_subprocess_args())
    m = re.search(r'^\s*iSerial\s+\d+\s+%s$' % serialname, output, flags=re.M)
    if m:
      return True

    return False
  else:
    if subprocess.call(['lsusb', '-d', vidpid], stdout=open('/dev/null', 'w')):
      return False
  return True

def check_usb_sn(vidpid):
  """Return the serial number

  Return the serial number of the first USB device with VID:PID vidpid,
  or None if no device is found. This will not work well with two of
  the same device attached.

  Args:
    vidpid: string representation of the usb vid:pid, eg. '18d1:2001'

  Returns: string serial number if found, None otherwise.
  """
  output = subprocess.check_output(['lsusb', '-v', '-d', vidpid],
                                   **get_subprocess_args())
  m = re.search(r'^\s*iSerial\s+(.*)$', output, flags=re.M)
  if m:
    return m.group(1)

  return None

def wait_for_usb_remove(vidpid, serialname=None, timeout=None):
  """Wait for USB device with vidpid to be removed.

  Wrapper for wait_for_usb below
  """
  wait_for_usb(vidpid, serialname=serialname,
               timeout=timeout, desiredpresence=False)

def wait_for_usb(vidpid, serialname=None, timeout=None, desiredpresence=True):
  """Wait for usb device with vidpid to be present/absent.

  Args:
    vidpid: string representation of the usb vid:pid, eg. '18d1:2001'
    serialname: serialname if specificed.
    timeout: timeout in seconds, None for no timeout.
    desiredpresence: True for present, False for not present.

  Raises:
    TinyServoError: on timeout.
  """
  if timeout:
    finish = datetime.datetime.now() + datetime.timedelta(seconds=timeout)
  while check_usb(vidpid, serialname) != desiredpresence:
    time.sleep(.1)
    if timeout:
      if datetime.datetime.now() > finish:
        raise TinyServoError('Timeout', 'Timeout waiting for USB %s' % vidpid)

def do_serialno(serialno, pty):
  """Set serialnumber 'serialno' via ec console 'pty'.

  Commands are:
  # > serialno set 1234
  # Saving serial number
  # Serial number: 1234

  Args:
    serialno: string serial number to set.
    pty: tinyservo console to send commands.

  Raises:
    TinyServoError: on failure to set.
    ptyError: on command interface error.
  """
  cmd = 'serialno set %s' % serialno
  regex = 'Serial number:\s+(\S+)'

  results = pty._issue_cmd_get_results(cmd, [regex])[0]
  sn = results[1].strip().strip('\n\r')

  if sn == serialno:
    log('Success !')
    log('Serial set to %s' % sn)
  else:
    log('Serial number set to %s but saved as %s.' % (serialno, sn))
    raise TinyServoError(
        'Serial Number',
        'Serial number set to %s but saved as %s.' % (serialno, sn))

def setup_tinyservod(vidpid, interface, serialname=None, debuglog=False):
  """Set up a pty

  Set up a pty to the ec console in order
  to send commands. Returns a pty_driver object.

  Args:
    vidpid: string vidpid of device to access.
    interface: not used.
    serialname: string serial name of device requested, optional.
    debuglog: chatty printout (boolean)

  Returns: pty object

  Raises:
    UsbError, SusbError: on device not found
  """
  vidstr, pidstr = vidpid.split(':')
  vid = int(vidstr, 16)
  pid = int(pidstr, 16)
  suart = stm32uart.Suart(vendor=vid, product=pid,
                          interface=interface, serialname=serialname,
                          debuglog=debuglog)
  suart.run()
  pty = pty_driver.ptyDriver(suart, [])

  return pty
