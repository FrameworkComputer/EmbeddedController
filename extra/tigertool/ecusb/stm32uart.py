# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Allow creation of uart/console interface via stm32 usb endpoint."""

from __future__ import print_function

import os
import select
import sys
import termios
import threading
import time
import tty

import usb  # pylint:disable=import-error

from . import stm32usb


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


class Suart:
    """Provide interface to stm32 serial usb endpoint."""

    def __init__(
        self,
        vendor=0x18D1,
        product=0x501A,
        interface=0,
        serialname=None,
        debuglog=False,
    ):
        """Suart contstructor.

        Initializes stm32 USB stream interface.

        Args:
          vendor: usb vendor id of stm32 device
          product: usb product id of stm32 device
          interface: interface number of stm32 device to use
          serialname: serial name to target. Defaults to None.
          debuglog: chatty output. Defaults to False.

        Raises:
          SuartError: If init fails
        """
        self._ptym = None
        self._ptys = None
        self._ptyname = None
        self._rx_thread = None
        self._tx_thread = None
        self._debuglog = debuglog
        self._susb = stm32usb.Susb(
            vendor=vendor,
            product=product,
            interface=interface,
            serialname=serialname,
        )
        self._running = False

    def __del__(self):
        """Suart destructor."""
        self.close()

    def close(self):
        """Stop all running threads."""
        self._running = False
        if self._rx_thread:
            self._rx_thread.join(2)
            self._rx_thread = None
        if self._tx_thread:
            self._tx_thread.join(2)
            self._tx_thread = None
        self._susb.close()

    def run_rx_thread(self):
        """Background loop to pass data from USB to pty."""
        ep = select.epoll()
        ep.register(self._ptym, select.EPOLLHUP)
        try:
            while self._running:
                events = ep.poll(0)
                # Check if the pty is connected to anything, or hungup.
                if not events:
                    try:
                        r = self._susb._read_ep.read(64, self._susb.TIMEOUT_MS)
                        if r:
                            if self._debuglog:
                                print("".join([chr(x) for x in r]), end="")
                            os.write(self._ptym, r)

                    # If we miss some characters on pty disconnect, that's fine.
                    # ep.read() also throws USBError on timeout, which we discard.
                    except (OSError, usb.core.USBError):
                        pass
                else:
                    time.sleep(0.1)
        except Exception as e:
            raise e

    def run_tx_thread(self):
        """Background loop to pass data from pty to USB."""
        ep = select.epoll()
        ep.register(self._ptym, select.EPOLLHUP)
        try:
            while self._running:
                events = ep.poll(0)
                # Check if the pty is connected to anything, or hungup.
                if not events:
                    try:
                        r = os.read(self._ptym, 64)
                        # TODO(crosbug.com/936182): Remove when the
                        # servo v4/micro console issues are fixed.
                        time.sleep(0.001)
                        if r:
                            self._susb._write_ep.write(r, self._susb.TIMEOUT_MS)

                    except (OSError, usb.core.USBError):
                        pass
                else:
                    time.sleep(0.1)
        except Exception as e:
            raise e

    def run(self):
        """Creates pthreads to poll stm32 & PTY for data."""
        m, s = os.openpty()
        self._ptyname = os.ttyname(s)

        self._ptym = m
        self._ptys = s

        os.fchmod(s, 0o660)

        # Change the owner and group of the PTY to the user who started servod.
        try:
            uid = int(os.environ.get("SUDO_UID", -1))
        except TypeError:
            uid = -1

        try:
            gid = int(os.environ.get("SUDO_GID", -1))
        except TypeError:
            gid = -1
        os.fchown(s, uid, gid)

        tty.setraw(self._ptym, termios.TCSADRAIN)

        # Generate a HUP flag on pty secondary fd.
        os.fdopen(s).close()

        self._running = True

        self._rx_thread = threading.Thread(target=self.run_rx_thread, args=[])
        self._rx_thread.daemon = True
        self._rx_thread.start()

        self._tx_thread = threading.Thread(target=self.run_tx_thread, args=[])
        self._tx_thread.daemon = True
        self._tx_thread.start()

    def get_uart_props(self):
        """Get the uart's properties.

        Returns:
          dict where:
            baudrate: integer of uarts baudrate
            bits: integer, number of bits of data Can be 5|6|7|8 inclusive
            parity: integer, parity of 0-2 inclusive where:
              0: no parity
              1: odd parity
              2: even parity
            sbits: integer, number of stop bits.  Can be 0|1|2 inclusive where:
              0: 1 stop bit
              1: 1.5 stop bits
              2: 2 stop bits
        """
        return {
            "baudrate": 115200,
            "bits": 8,
            "parity": 0,
            "sbits": 1,
        }

    def set_uart_props(self, line_props):
        """Set the uart's properties.

        Note that Suart cannot set properties
        and will fail if the properties are not the default 115200,8n1.

        Args:
          line_props: dict where:
            baudrate: integer of uarts baudrate
            bits: integer, number of bits of data ( prior to stop bit)
            parity: integer, parity of 0-2 inclusive where
              0: no parity
              1: odd parity
              2: even parity
            sbits: integer, number of stop bits.  Can be 0|1|2 inclusive where:
              0: 1 stop bit
              1: 1.5 stop bits
              2: 2 stop bits

        Raises:
          SuartError: If requested line properties are not the default.
        """
        curr_props = self.get_uart_props()
        for prop in line_props:
            if line_props[prop] != curr_props[prop]:
                raise SuartError(
                    "Line property %s cannot be set from %s to %s"
                    % (prop, curr_props[prop], line_props[prop])
                )
        return True

    def get_pty(self):
        """Gets path to pty for communication to/from uart.

        Returns:
          String path to the pty connected to the uart
        """
        return self._ptyname


def main():
    """Run a suart test with the default parameters."""
    try:
        sobj = Suart()
        sobj.run()

        # run() is a thread so just busy wait to mimic server.
        while True:
            # Ours sleeps to eleven!
            time.sleep(11)
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
