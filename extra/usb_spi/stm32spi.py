#!/usr/bin/env python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


# Test program to access SPI via stm32 raiden debug spi.

import usb
import usb.core
import usb.util

class SSpiBus(object):
  """SPI bus class to access devices on the bus.

  Usage:
    bus = SSpiBus()
    # read 1 byte from register(0x16)
    bus.wr_rd([0x16], 1)
    # write 2 bytes to register(0x20)
    bus.wr_rd([0x20, 0x01, 0x02])

  Instance Variables:
    _dev: pyUSB device object
    _read_ep: pyUSB read endpoint for this interface
    _write_ep: pyUSB write endpoint for this interface
  """
  def __init__(self, vendor=0x18d1,
               product=0x501a, interface=2, serialname=None):
    # Find the stm32.
    dev = usb.core.find(idVendor=vendor, idProduct=product)
    if dev is None:
      raise Exception("SPI", "USB device not found")

    print "Found stm32: %04x:%04x" % (vendor, product)
    self._dev = dev

    # Get an endpoint instance.
    cfg = dev.get_active_configuration()
    intf = usb.util.find_descriptor(cfg, bInterfaceNumber=interface)
    self._intf = intf
    print "InterfaceNumber: %s" % intf.bInterfaceNumber

    read_ep = usb.util.find_descriptor(
        intf,
        # match the first IN endpoint
        custom_match=\
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_IN
    )

    self._read_ep = read_ep
    print "Reader endpoint: 0x%x" % read_ep.bEndpointAddress

    write_ep = usb.util.find_descriptor(
        intf,
        # match the first OUT endpoint
        custom_match=\
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_OUT
    )

    self._write_ep = write_ep
    print "Writer endpoint: 0x%x" % write_ep.bEndpointAddress

    self.enable(True)
    print "Set up stm32 spi"

  def enable(self, enable):
    # USB_RIR_OUT = 0 | USB_TYPE_VENDOR = (0x02 << 5) |
    #     USB_RECIP_INTERFACE = 0x01
    bmRequestType = 0x41
    # USB_SPI_REQ_ENABLE  = 0x0000, USB_SPI_REQ_DISABLE = 0x0001
    if enable:
      bmRequest = 0x0
    else:
      bmRequest = 0x1

    print "ctrl_transfer(0x%x, 0x%x, 0, 0x%x, null)" % (
        bmRequestType, bmRequest, self._intf.bInterfaceNumber)
    ret = self._dev.ctrl_transfer(
        bmRequestType, bmRequest, 0, self._intf.bInterfaceNumber, '')
    print "ctrl_transfer ret - %s" % ret




  def wr_rd(self, write_list, read_count=None):
    """Implements hdctools wr_rd() interface.

    This function writes byte values list to I2C device, then reads
    byte values from the same device.

    Args:
      write_list: list of output byte values [0~255].
      read_count: number of byte values to read from device.

    Interface:
      write: [write_count, read_count, data ... ]
      read: [data .. ]
    """
    print "SSpi.wr_rd(write_list=%s, read_count=%s)" % (
        write_list, read_count)

    # Clean up args from python style to correct types.
    write_length = 0
    if write_list:
      write_length = len(write_list)
    if not read_count:
      read_count = 0

    # Send wr_rd command to stm32.
    cmd = [write_length, read_count] + write_list
    print "WR: %s" % cmd
    ret = self._write_ep.write(cmd, 100)

    print "RET: %s " % ret

    # Read back response if necessary.
    bytesread = self._read_ep.read(read_count + 2, 1000)
    print "BYTES: %s " % bytesread

    if len(bytesread) < 2:
      raise Exception("SPI", "Read status failed.")

    print "STATUS: 0x%02x%02x" % (int(bytesread[1]), int(bytesread[0]))
    return bytesread[2:]


def main():
  bus = SSpiBus()
  # write 2 bytes to register(0x20)
  bus.wr_rd([0x90, 0x00, 0x00, 0x00], 2)
  # read 1 byte from register(0x16)
  bus.wr_rd([0x9f], 3)

if __name__ == "__main__":
  main()
