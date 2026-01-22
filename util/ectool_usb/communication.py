# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A library for ectool usb communication."""

from enum import IntEnum

# pylint: disable=import-error
# PyUsb is not available in CROS SDK
import usb.core


# pylint: enable=import-error


GOOGLE_VID = 0x18D1
USB_SUBCLASS_GOOGLE_EC_HOST_CMD = 0x5A
USB_BCC_VENDOR = 0xFF


class HostCommandIRQType(IntEnum):
    """Event types sent via the interrupt endpoint"""

    EVENT = 0
    RESPONSE_READY = 1


class UsbCommunicationError(IOError):
    """USB communication error."""


class FindHCInf:
    """A custom match functor for usb.core.find."""

    def __call__(self, device) -> bool:
        """Finds the Host Command interface."""
        # first, let's check the device
        for cfg in device:
            # find_descriptor: what's it?
            intf = usb.util.find_descriptor(
                cfg,
                bInterfaceClass=USB_BCC_VENDOR,
                bInterfaceSubClass=USB_SUBCLASS_GOOGLE_EC_HOST_CMD,
            )
            if intf is not None:
                return True

        return False


class UsbCommunication:
    """Class for USB Communication."""

    SEND_TIMEOUT_MS = 100
    WAIT_TIMEOUT_MS = 200
    RECEIVE_TIMEOUT_MS = 100

    def __init__(self):
        """Initializes the USB communication."""
        self.connect()

    def __enter__(self) -> "UsbCommunication":
        """Returns the UsbCommunication object."""
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Disposes of the USB resources."""
        if self.ec_dev:
            usb.util.dispose_resources(self.ec_dev)

    def connect(self):
        """Connects to the EC."""
        self.ec_dev = usb.core.find(
            idVendor=GOOGLE_VID, custom_match=FindHCInf()
        )
        if self.ec_dev is None:
            raise UsbCommunicationError("Failed to find HC interface")

        cfg = self.ec_dev.get_active_configuration()
        intf = usb.util.find_descriptor(
            cfg,
            bInterfaceClass=USB_BCC_VENDOR,
            bInterfaceSubClass=USB_SUBCLASS_GOOGLE_EC_HOST_CMD,
        )
        if intf is None:
            raise UsbCommunicationError("Failed to find interface descriptor")

        self.ep_out = usb.util.find_descriptor(
            intf,
            custom_match=lambda ep: usb.util.endpoint_direction(
                ep.bEndpointAddress
            )
            == usb.util.ENDPOINT_OUT,
        )
        if self.ep_out is None:
            raise UsbCommunicationError("Failed to find OUT endpoint")
        self.ep_in_bulk = usb.util.find_descriptor(
            intf,
            custom_match=lambda ep: usb.util.endpoint_direction(
                ep.bEndpointAddress
            )
            == usb.util.ENDPOINT_IN
            and usb.util.endpoint_type(ep.bmAttributes)
            == usb.util.ENDPOINT_TYPE_BULK,
        )
        if self.ep_in_bulk is None:
            raise UsbCommunicationError("Failed to find BULK IN endpoint")
        self.ep_in_int = usb.util.find_descriptor(
            intf,
            custom_match=lambda ep: usb.util.endpoint_type(ep.bmAttributes)
            == usb.util.ENDPOINT_TYPE_INTR,
        )
        if self.ep_in_int is None:
            raise UsbCommunicationError("Failed to find INTERRUPT IN endpoint")

    def send(self, cmd_bytes) -> int:
        """Sends data to the EC."""
        return self.ep_out.write(cmd_bytes, timeout=self.SEND_TIMEOUT_MS)

    def _wait_for_interrupt(self, timeout: int) -> HostCommandIRQType:
        """Waits for an interrupt."""
        try:
            ret = self.ep_in_int.read(
                self.ep_in_int.wMaxPacketSize, timeout=timeout
            )
        except usb.core.USBTimeoutError:
            return None
        return ret[0]

    def wait(self):
        """Waits for a response from the EC."""
        # Wait for response ready signal from interrupt EP
        while (
            self._wait_for_interrupt(self.WAIT_TIMEOUT_MS)
            != HostCommandIRQType.RESPONSE_READY
        ):
            pass

    def wait_for_event(self, timeout: int) -> bool:
        """Waits for an event from EC."""
        return self._wait_for_interrupt(timeout) == HostCommandIRQType.EVENT

    def receive(self, size=-1) -> bytes:
        """Receives data from the EC."""
        if size < 0:
            size = self.ep_in_bulk.wMaxPacketSize
        return self.ep_in_bulk.read(size, timeout=self.RECEIVE_TIMEOUT_MS)

    @property
    def vid(self) -> int:
        """Returns the vendor ID."""
        return self.ec_dev.idVendor

    @property
    def pid(self) -> int:
        """Returns the product ID."""
        return self.ec_dev.idProduct

    @property
    def serial_number(self) -> str:
        """Returns the serial number."""
        return self.ec_dev.serial_number
