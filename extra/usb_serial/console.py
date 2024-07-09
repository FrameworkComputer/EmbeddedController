#!/usr/bin/env python3
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Allow creation of uart/console interface via usb google serial endpoint."""

import argparse
import array
import os
import sys
import termios
import threading
import time
import tty


try:
    import usb  # pylint:disable=import-error
except ModuleNotFoundError:
    print("import usb failed")
    print("try running these commands:")
    print(" sudo apt-get install python-pip")
    print(" sudo pip install --pre pyusb")
    print()
    sys.exit(-1)


class SusbError(Exception):
    """Class for exceptions of Susb."""

    def __init__(self, msg, value=0):
        """SusbError constructor.

        Args:
          msg: string, message describing error in detail
          value: integer, value of error when non-zero status returned.  Default=0
        """
        super().__init__(msg, value)
        self.msg = msg
        self.value = value


class Susb:
    """Provide USB functionality.

    Instance Variables:
    _read_ep: pyUSB read endpoint for this interface
    _write_ep: pyUSB write endpoint for this interface
    """

    READ_ENDPOINT = 0x81
    WRITE_ENDPOINT = 0x1
    TIMEOUT_MS = 100

    def __init__(
        self, vendor=0x18D1, product=0x500F, interface=1, serialname=None
    ):
        """Susb constructor.

        Discovers and connects to USB endpoints.

        Args:
          vendor: usb vendor id of device
          product: usb product id of device
          interface: interface number ( 1 - 8 ) of device to use
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
            for device in dev_list:
                dev_serial = usb.util.get_string(device, device.iSerialNumber)
                if dev_serial == serialname:
                    dev = device
                    break
            if dev is None:
                raise SusbError(f"USB device({serialname}) not found")
        else:
            try:
                dev = dev_list[0]
            except IndexError as err:
                raise SusbError(
                    f"USB device {vendor:04x}:{product:04x} not found"
                ) from err

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
        if dev.is_kernel_driver_active(intf.bInterfaceNumber):
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


class SuartError(Exception):
    """Class for exceptions of Suart."""

    def __init__(self, msg, value=0):
        """SuartError constructor.

        Args:
          msg: string, message describing error in detail
          value: integer, value of error when non-zero status returned.  Default=0
        """
        super().__init__(msg, value)
        self.msg = msg
        self.value = value


class Suart:
    """Provide interface to serial usb endpoint."""

    def __init__(
        self, vendor=0x18D1, product=0x501C, interface=0, serialname=None
    ):
        """Suart constructor.

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
        self._susb = Susb(
            vendor=vendor,
            product=product,
            interface=interface,
            serialname=serialname,
        )
        self._exit = False
        self._rx_thread = None
        self._tx_thread = None

    def wait_until_done(self, timeout=None):
        """Wait until the background threads are done."""
        return self._done.wait(timeout=timeout)

    def run_rx_thread(self):
        """Runs the reading background thread."""
        try:
            while True:
                try:
                    data = self._susb._read_ep.read(  # pylint:disable=protected-access
                        64, self._susb.TIMEOUT_MS
                    )
                    if data:
                        sys.stdout.buffer.write(data.tobytes())
                        sys.stdout.buffer.flush()

                except Exception as err:  # pylint:disable=broad-except
                    # If we miss some characters on pty disconnect, that's fine.
                    # ep.read() also throws USBError on timeout, which we discard.
                    if not isinstance(err, (OSError, usb.core.USBError)):
                        print(f"rx {err}")
        finally:
            self._done.set()

    def run_tx_thread(self):
        """Runs the writing background thread."""
        try:
            while True:
                try:
                    data = sys.stdin.buffer.read(1)
                    if not data or data == b"\x03":
                        break
                    if data:
                        self._susb._write_ep.write(  # pylint:disable=protected-access
                            array.array("B", data), self._susb.TIMEOUT_MS
                        )
                except Exception as err:  # pylint:disable=broad-except
                    print(f"tx {err}")
        finally:
            self._done.set()

    def run(self):
        """Creates pthreads to poll USB & PTY for data."""
        self._exit = False

        self._rx_thread = threading.Thread(target=self.run_rx_thread)
        self._rx_thread.daemon = True
        self._rx_thread.start()

        self._tx_thread = threading.Thread(target=self.run_tx_thread)
        self._tx_thread.daemon = True
        self._tx_thread.start()


parser = argparse.ArgumentParser(
    description="Open a console to a USB device",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)
parser.add_argument(
    "-d",
    "--device",
    type=str,
    help="vid:pid of target device",
    default="18d1:501c",
)
parser.add_argument(
    "-i", "--interface", type=int, help="interface number of console", default=0
)
parser.add_argument(
    "-s", "--serialno", type=str, help="serial number of device", default=""
)
parser.add_argument(
    "-S",
    "--notty-exit-sleep",
    type=float,
    default=0.2,
    help="When stdin is *not* a TTY, wait this many seconds "
    "after EOF from stdin before exiting, to give time for "
    "receiving a reply from the USB device.",
)


def runconsole():
    """Run the usb console code

    Starts the pty thread, and idles until a ^C is caught.
    """
    args = parser.parse_args()

    vidstr, pidstr = args.device.split(":")
    vid = int(vidstr, 16)
    pid = int(pidstr, 16)

    serialno = args.serialno
    interface = args.interface

    sobj = Suart(
        vendor=vid, product=pid, interface=interface, serialname=serialno
    )
    if sys.stdin.isatty():
        tty.setraw(sys.stdin.fileno())
    sobj.run()
    sobj.wait_until_done()
    if not sys.stdin.isatty() and args.notty_exit_sleep > 0:
        time.sleep(args.notty_exit_sleep)


def main():
    """The main function."""
    stdin_isatty = sys.stdin.isatty()
    if stdin_isatty:
        ffd = sys.stdin.fileno()
        os.system("stty -echo")
        old_settings = termios.tcgetattr(ffd)

    try:
        runconsole()
    finally:
        if stdin_isatty:
            termios.tcsetattr(ffd, termios.TCSADRAIN, old_settings)
            os.system("stty echo")
        # Avoid having the user's shell prompt start mid-line after the final output
        # from this program.
        print()


if __name__ == "__main__":
    main()
