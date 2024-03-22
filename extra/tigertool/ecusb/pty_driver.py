# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""ptyDriver class

This class takes a pty interface and can send commands and expect results
as regex. This is useful for automating console based interfaces, such as
the CrOS EC console commands.
"""

import ast
import errno
import fcntl
import os
import time

import pexpect  # pylint:disable=import-error
from pexpect import fdpexpect  # pylint:disable=import-error


# Expecting a result in 3 seconds is plenty even for slow platforms.
DEFAULT_UART_TIMEOUT = 3
FLUSH_UART_TIMEOUT = 1


class ptyError(Exception):
    """Exception class for pty errors."""


UART_PARAMS = {
    "uart_cmd": None,
    "uart_multicmd": None,
    "uart_regexp": None,
    "uart_timeout": DEFAULT_UART_TIMEOUT,
}


class ptyDriver:
    """Automate interactive commands on a pty interface."""

    def __init__(self, interface, _unused_params, fast=False):
        """Init class variables."""
        self._child = None
        self._fd = None
        self._interface = interface
        self._pty_path = self._interface.get_pty()
        self._dict = UART_PARAMS.copy()
        self._fast = fast

    def __del__(self):
        self.close()

    def close(self):
        """Close any open files and interfaces."""
        if self._fd:
            self._close()
        self._interface.close()

    def _open(self):
        """Connect to serial device and create pexpect interface."""
        assert self._fd is None
        self._fd = os.open(self._pty_path, os.O_RDWR | os.O_NONBLOCK)
        # Let the USB device settle, old firmware needs it
        time.sleep(0.1)
        # Don't allow forked processes to access.
        fcntl.fcntl(
            self._fd,
            fcntl.F_SETFD,
            fcntl.fcntl(self._fd, fcntl.F_GETFD) | fcntl.FD_CLOEXEC,
        )
        self._child = fdpexpect.fdspawn(self._fd)
        # pexpect defaults to a 100ms delay before sending characters, to
        # work around race conditions in ssh. We don't need this feature
        # so we'll change delaybeforesend from 0.1 to 0.001 to speed things up.
        if self._fast:
            self._child.delaybeforesend = 0.001

    def _close(self):
        """Close serial device connection."""
        os.close(self._fd)
        self._fd = None
        self._child = None

    def _flush(self):
        """Flush device output to prevent previous messages interfering."""
        if self._child.sendline("") != 1:
            raise ptyError("Failed to send newline.")
        # Have a maximum timeout for the flush operation. We should have cleared
        # all data from the buffer, but if data is regularly being generated, we
        # can't guarantee it will ever stop.
        flush_end_time = time.time() + FLUSH_UART_TIMEOUT
        while time.time() <= flush_end_time:
            try:
                self._child.expect(".", timeout=0.01)
            except (pexpect.TIMEOUT, pexpect.EOF):
                break
            except OSError as e:
                # EAGAIN indicates no data available, maybe we didn't wait long enough.
                if e.errno != errno.EAGAIN:
                    raise
                break

    def _send(self, cmds):
        """Send command to EC.

        This function always flushes serial device before sending, and is used as
        a wrapper function to make sure the channel is always flushed before
        sending commands.

        Args:
          cmds: The commands to send to the device, either a list or a string.

        Raises:
          ptyError: Raised when writing to the device fails.
        """
        self._flush()
        if not isinstance(cmds, list):
            cmds = [cmds]
        for cmd in cmds:
            if self._child.sendline(cmd) != len(cmd) + 1:
                raise ptyError("Failed to send command.")

    def _issue_cmd(self, cmds):
        """Send command to the device and do not wait for response.

        Args:
          cmds: The commands to send to the device, either a list or a string.
        """
        self._issue_cmd_get_results(cmds, [])

    def _issue_cmd_get_results(
        self, cmds, regex_list, timeout=DEFAULT_UART_TIMEOUT
    ):
        r"""Send command to the device and wait for response.

        This function waits for response message matching a regular
        expressions.

        Args:
          cmds: The commands issued, either a list or a string.
          regex_list: List of Regular expressions used to match response message.
            Note1, list must be ordered.
            Note2, empty list sends and returns.
          timeout: time to wait for matching results before failing.

        Returns:
          List of tuples, each of which contains the entire matched string and
          all the subgroups of the match. None if not matched.
          For example:
            response of the given command:
              High temp: 37.2
              Low temp: 36.4
            regex_list:
              [r'High temp: (\d+)\.(\d+)', r'Low temp: (\d+)\.(\d+)']
            returns:
              [('High temp: 37.2', '37', '2'), ('Low temp: 36.4', '36', '4')]

        Raises:
          ptyError: If timed out waiting for a response
        """
        result_list = []
        self._open()
        try:
            self._send(cmds)
            for regex in regex_list:
                self._child.expect(regex, timeout)
                match = self._child.match
                lastindex = match.lastindex if match and match.lastindex else 0
                # Create a tuple which contains the entire matched string and all
                # the subgroups of the match.
                result = match.group(*range(lastindex + 1)) if match else None
                if result is not None:
                    result = tuple(res.decode("utf-8") for res in result)
                result_list.append(result)
        except pexpect.TIMEOUT:
            raise ptyError("Timeout waiting for response.")
        finally:
            if not regex_list:
                # Must be longer than delaybeforesend
                time.sleep(0.1)
            self._close()
        return result_list

    def _issue_cmd_get_multi_results(self, cmd, regex):
        """Send command to the device and wait for multiple response.

        This function waits for arbitrary number of response message
        matching a regular expression.

        Args:
          cmd: The command issued.
          regex: Regular expression used to match response message.

        Returns:
          List of tuples, each of which contains the entire matched string and
          all the subgroups of the match. None if not matched.
        """
        result_list = []
        self._open()
        try:
            self._send(cmd)
            while True:
                try:
                    self._child.expect(regex, timeout=0.1)
                    match = self._child.match
                    lastindex = (
                        match.lastindex if match and match.lastindex else 0
                    )
                    # Create a tuple which contains the entire matched string and all
                    # the subgroups of the match.
                    result = (
                        match.group(*range(lastindex + 1)) if match else None
                    )
                    if result is not None:
                        result = tuple(res.decode("utf-8") for res in result)
                    result_list.append(result)
                except pexpect.TIMEOUT:
                    break
        finally:
            self._close()
        return result_list

    def _Set_uart_timeout(self, timeout):
        """Set timeout value for waiting for the device response.

        Args:
          timeout: Timeout value in second.
        """
        self._dict["uart_timeout"] = timeout

    def _Get_uart_timeout(self):
        """Get timeout value for waiting for the device response.

        Returns:
          Timeout value in second.
        """
        return self._dict["uart_timeout"]

    def _Set_uart_regexp(self, regexp):
        """Set the list of regular expressions which matches the command response.

        Args:
          regexp: A string which contains a list of regular expressions.
        """
        if not isinstance(regexp, str):
            raise ptyError("The argument regexp should be a string.")
        self._dict["uart_regexp"] = ast.literal_eval(regexp)

    def _Get_uart_regexp(self):
        """Get the list of regular expressions which matches the command response.

        Returns:
          A string which contains a list of regular expressions.
        """
        return str(self._dict["uart_regexp"])

    def _Set_uart_cmd(self, cmd):
        """Set the UART command and send it to the device.

        If ec_uart_regexp is 'None', the command is just sent and it doesn't care
        about its response.

        If ec_uart_regexp is not 'None', the command is send and its response,
        which matches the regular expression of ec_uart_regexp, will be kept.
        Use its getter to obtain this result. If no match after ec_uart_timeout
        seconds, a timeout error will be raised.

        Args:
          cmd: A string of UART command.
        """
        if self._dict["uart_regexp"]:
            self._dict["uart_cmd"] = self._issue_cmd_get_results(
                cmd, self._dict["uart_regexp"], self._dict["uart_timeout"]
            )
        else:
            self._dict["uart_cmd"] = None
            self._issue_cmd(cmd)

    def _Set_uart_multicmd(self, cmds):
        """Set multiple UART commands and send them to the device.

        Note that ec_uart_regexp is not supported to match the results.

        Args:
          cmds: A semicolon-separated string of UART commands.
        """
        self._issue_cmd(cmds.split(";"))

    def _Get_uart_cmd(self):
        """Get the result of the latest UART command.

        Returns:
          A string which contains a list of tuples, each of which contains the
          entire matched string and all the subgroups of the match. 'None' if
          the ec_uart_regexp is 'None'.
        """
        return str(self._dict["uart_cmd"])

    def _Set_uart_capture(self, cmd):
        """Set UART capture mode (on or off).

        Once capture is enabled, UART output could be collected periodically by
        invoking _Get_uart_stream() below.

        Args:
          cmd: True for on, False for off
        """
        self._interface.set_capture_active(cmd)

    def _Get_uart_capture(self):
        """Get the UART capture mode (on or off)."""
        return self._interface.get_capture_active()

    def _Get_uart_stream(self):
        """Get uart stream generated since last time."""
        return self._interface.get_stream()
