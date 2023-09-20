# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Allows creation of an interface via stm32 usb."""

import usb  # pylint:disable=import-error


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


class Susb:
    """Provide stm32 USB functionality.

    Instance Variables:
      _read_ep: pyUSB read endpoint for this interface
      _write_ep: pyUSB write endpoint for this interface
    """

    READ_ENDPOINT = 0x81
    WRITE_ENDPOINT = 0x1
    TIMEOUT_MS = 100

    def __init__(
        self,
        vendor=0x18D1,
        product=0x5027,
        interface=1,
        serialname=None,
        _unused_logger=None,
    ):
        """Susb constructor.

        Discovers and connects to stm32 USB endpoints.

        Args:
          vendor: usb vendor id of stm32 device.
          product: usb product id of stm32 device.
          interface: interface number ( 1 - 4 ) of stm32 device to use.
          serialname: string of device serialname.
          logger: none

        Raises:
          SusbError: An error accessing Susb object
        """
        self._vendor = vendor
        self._product = product
        self._interface = interface
        self._serialname = serialname
        self._find_device()

    def _find_device(self):
        """Set up the usb endpoint"""
        # Find the stm32.
        dev_g = usb.core.find(
            idVendor=self._vendor, idProduct=self._product, find_all=True
        )
        dev_list = list(dev_g)

        if not dev_list:
            raise SusbError("USB device not found")

        # Check if we have multiple stm32s and we've specified the serial.
        dev = None
        if self._serialname:
            for d in dev_list:
                dev_serial = usb.util.get_string(d, d.iSerialNumber)
                if dev_serial == self._serialname:
                    dev = d
                    break
            if dev is None:
                raise SusbError("USB device(%s) not found" % self._serialname)
        else:
            try:
                dev = dev_list[0]
            except StopIteration:
                raise SusbError(
                    "USB device %04x:%04x not found"
                    % (self._vendor, self._product)
                )

        # If we can't set configuration, it's already been set.
        try:
            dev.set_configuration()
        except usb.core.USBError:
            pass

        self._dev = dev

        # Get an endpoint instance.
        cfg = dev.get_active_configuration()
        intf = usb.util.find_descriptor(cfg, bInterfaceNumber=self._interface)
        self._intf = intf
        if not intf:
            raise SusbError(
                "Interface %04x:%04x - 0x%x not found"
                % (self._vendor, self._product, self._interface)
            )

        # Detach raiden.ko if it is loaded. CCD endpoints support either a kernel
        # module driver that produces a ttyUSB, or direct endpoint access, but
        # can't do both at the same time.
        if dev.is_kernel_driver_active(intf.bInterfaceNumber) is True:
            dev.detach_kernel_driver(intf.bInterfaceNumber)

        read_ep_number = intf.bInterfaceNumber + self.READ_ENDPOINT
        read_ep = usb.util.find_descriptor(
            intf, bEndpointAddress=read_ep_number
        )
        self._read_ep = read_ep

        write_ep_number = intf.bInterfaceNumber + self.WRITE_ENDPOINT
        write_ep = usb.util.find_descriptor(
            intf, bEndpointAddress=write_ep_number
        )
        self._write_ep = write_ep

    def close(self):
        usb.util.dispose_resources(self._dev)
