# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Ignore indention messages, since legacy scripts use 2 spaces instead of 4.
# pylint: disable=bad-indentation,docstring-section-indent
# pylint: disable=docstring-trailing-quotes

"""Helper class to facilitate communication to servo ec console."""

from ecusb import pty_driver
from ecusb import stm32uart


class TinyServod(object):
  """Helper class to wrap a pty_driver with interface."""

  def __init__(self, vid, pid, interface, serialname=None, debug=False):
    """Build the driver and interface.

    Args:
      vid: servo device vid
      pid: servo device pid
      interface: which usb interface the servo console is on
      serialname: the servo device serial (if available)
    """
    self._vid = vid
    self._pid = pid
    self._interface = interface
    self._serial = serialname
    self._debug = debug
    self._init()

  def _init(self):
    self.suart = stm32uart.Suart(vendor=self._vid,
                                 product=self._pid,
                                 interface=self._interface,
                                 serialname=self._serial,
                                 debuglog=self._debug)
    self.suart.run()
    self.pty = pty_driver.ptyDriver(self.suart, [])

  def reinitialize(self):
    """Reinitialize the connect after a reset/disconnect/etc."""
    self.close()
    self._init()

  def close(self):
    """Close out the connection and release resources.

    Note: if another TinyServod process or servod itself needs the same device
          it's necessary to call this to ensure the usb device is available.
    """
    self.suart.close()
