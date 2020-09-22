#!/usr/bin/env python
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Allow creation of uart/console interface via usb google serial endpoint."""

# Note: This is a py2/3 compatible file.

from __future__ import print_function
import argparse
import array
import os
import sys
import termios
import threading
import time
import traceback
import tty
try:
  import usb
except:
  print("import usb failed")
  print("try running these commands:")
  print(" sudo apt-get install python-pip")
  print(" sudo pip install --pre pyusb")
  print()
  sys.exit(-1)

import six


def GetBuffer(stream):
  if six.PY3:
    return stream.buffer
  return stream


"""Class Susb covers USB device discovery and initialization.

  It can find a particular endpoint by vid:pid, serial number,
  and interface number.
"""

class SusbError(Exception):
  """Class for exceptions of Susb."""
  def __init__(self, msg, value=0):
    """SusbError constructor.

    Args:
      msg: string, message describing error in detail
      value: integer, value of error when non-zero status returned.  Default=0
    """
    super(SusbError, self).__init__(msg, value)
    self.msg = msg
    self.value = value

class Susb():
  """Provide USB functionality.

  Instance Variables:
  _read_ep: pyUSB read endpoint for this interface
  _write_ep: pyUSB write endpoint for this interface
  """
  READ_ENDPOINT = 0x81
  WRITE_ENDPOINT = 0x1
  TIMEOUT_MS = 100

  def __init__(self, vendor=0x18d1,
               product=0x500f, interface=1, serialname=None):
    """Susb constructor.

    Discovers and connects to USB endpoints.

    Args:
      vendor    : usb vendor id of device
      product   : usb product id of device
      interface : interface number ( 1 - 8 ) of device to use
      serialname: string of device serialnumber.

    Raises:
      SusbError: An error accessing Susb object
    """
    # Find the device.
    dev_g = usb.core.find(idVendor=vendor, idProduct=product, find_all=True)
    dev_list = list(dev_g)
    if dev_list is None:
      raise SusbError("USB device not found")

    # Check if we have multiple devices.
    dev = None
    if serialname:
      for d in dev_list:
        dev_serial = "PyUSB doesn't have a stable interface"
        try:
          dev_serial = usb.util.get_string(d, 256, d.iSerialNumber)
        except:
          dev_serial = usb.util.get_string(d, d.iSerialNumber)
        if dev_serial == serialname:
          dev = d
          break
      if dev is None:
        raise SusbError("USB device(%s) not found" % (serialname,))
    else:
      try:
        dev = dev_list[0]
      except:
        try:
          dev = dev_list.next()
        except:
          raise SusbError("USB device %04x:%04x not found" % (vendor, product))

    # If we can't set configuration, it's already been set.
    try:
      dev.set_configuration()
    except usb.core.USBError:
      pass

    # Get an endpoint instance.
    cfg = dev.get_active_configuration()
    intf = usb.util.find_descriptor(cfg, bInterfaceNumber=interface)
    self._intf = intf

    if not intf:
      raise SusbError("Interface not found")

    # Detach raiden.ko if it is loaded.
    if dev.is_kernel_driver_active(intf.bInterfaceNumber) is True:
            dev.detach_kernel_driver(intf.bInterfaceNumber)

    read_ep_number = intf.bInterfaceNumber + self.READ_ENDPOINT
    read_ep = usb.util.find_descriptor(intf, bEndpointAddress=read_ep_number)
    self._read_ep = read_ep

    write_ep_number = intf.bInterfaceNumber + self.WRITE_ENDPOINT
    write_ep = usb.util.find_descriptor(intf, bEndpointAddress=write_ep_number)
    self._write_ep = write_ep


"""Suart class implements a stream interface, to access Google's USB class.

  This creates a send and receive thread that monitors USB and console input
  and forwards them across. This particular class is hardcoded to stdin/out.
"""

class SuartError(Exception):
  """Class for exceptions of Suart."""
  def __init__(self, msg, value=0):
    """SuartError constructor.

    Args:
      msg: string, message describing error in detail
      value: integer, value of error when non-zero status returned.  Default=0
    """
    super(SuartError, self).__init__(msg, value)
    self.msg = msg
    self.value = value


class Suart():
  """Provide interface to serial usb endpoint."""

  def __init__(self, vendor=0x18d1, product=0x501c, interface=0,
               serialname=None):
    """Suart contstructor.

    Initializes USB stream interface.

    Args:
      vendor: usb vendor id of device
      product: usb product id of device
      interface: interface number of device to use
      serialname: Defaults to None.

    Raises:
      SuartError: If init fails
    """
    self._done = threading.Event()
    self._susb = Susb(vendor=vendor, product=product,
        interface=interface, serialname=serialname)

  def wait_until_done(self, timeout=None):
    return self._done.wait(timeout=timeout)

  def run_rx_thread(self):
    try:
      while True:
          try:
            r = self._susb._read_ep.read(64, self._susb.TIMEOUT_MS)
            if r:
              GetBuffer(sys.stdout).write(r.tostring())
              GetBuffer(sys.stdout).flush()

          except Exception as e:
            # If we miss some characters on pty disconnect, that's fine.
            # ep.read() also throws USBError on timeout, which we discard.
            if not isinstance(e, (OSError, usb.core.USBError)):
              print("rx %s" % e)
    finally:
      self._done.set()

  def run_tx_thread(self):
    try:
      while True:
          try:
            r = GetBuffer(sys.stdin).read(1)
            if not r or r == b"\x03":
              break
            if r:
              self._susb._write_ep.write(array.array('B', r),
                                         self._susb.TIMEOUT_MS)
          except Exception as e:
            print("tx %s" % e)
    finally:
      self._done.set()

  def run(self):
    """Creates pthreads to poll USB & PTY for data.
    """
    self._exit = False

    self._rx_thread = threading.Thread(target=self.run_rx_thread)
    self._rx_thread.daemon = True
    self._rx_thread.start()

    self._tx_thread = threading.Thread(target=self.run_tx_thread)
    self._tx_thread.daemon = True
    self._tx_thread.start()



"""Command line functionality

  Allows specifying vid:pid, serialnumber, interface.
  Ctrl-C exits.
"""

parser = argparse.ArgumentParser(description="Open a console to a USB device")
parser.add_argument('-d', '--device', type=str,
    help="vid:pid of target device", default="18d1:501c")
parser.add_argument('-i', '--interface', type=int,
    help="interface number of console", default=0)
parser.add_argument('-s', '--serialno', type=str,
    help="serial number of device", default="")
parser.add_argument('-S', '--notty-exit-sleep', type=float, default=0.2,
    help="When stdin is *not* a TTY, wait this many seconds after EOF from "
    "stdin before exiting, to give time for receiving a reply from the USB "
    "device.")


def runconsole():
  """Run the usb console code

  Starts the pty thread, and idles until a ^C is caught.
  """
  args = parser.parse_args()

  vidstr, pidstr = args.device.split(':')
  vid = int(vidstr, 16)
  pid = int(pidstr, 16)

  serialno = args.serialno
  interface = args.interface

  sobj = Suart(vendor=vid, product=pid, interface=interface,
               serialname=serialno)
  if sys.stdin.isatty():
    tty.setraw(sys.stdin.fileno())
  sobj.run()
  sobj.wait_until_done()
  if not sys.stdin.isatty() and args.notty_exit_sleep > 0:
    time.sleep(args.notty_exit_sleep)


def main():
  stdin_isatty = sys.stdin.isatty()
  if stdin_isatty:
    fd = sys.stdin.fileno()
    os.system("stty -echo")
    old_settings = termios.tcgetattr(fd)

  try:
    runconsole()
  finally:
    if stdin_isatty:
      termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
      os.system("stty echo")
    # Avoid having the user's shell prompt start mid-line after the final output
    # from this program.
    print()


if __name__ == '__main__':
  main()
