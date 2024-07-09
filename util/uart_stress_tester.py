#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""ChromeOS Uart Stress Test

This tester runs the command 'chargen' on EC and/or AP, captures the
output, and compares it against the expected output to check any characters
lost.

Prerequisite:
    (1) This test needs PySerial. Please check if it is available before test.
        Can be installed by 'pip install pyserial'
    (2) If servod is running, turn uart_timestamp off before running this test.
        e.g. dut-control cr50_uart_timestamp:off
"""

import argparse
import atexit
import logging
import os
import stat
import sys
import threading
import time

import serial  # pylint:disable=import-error


BAUDRATE = 115200  # Default baudrate setting for UART port
CROS_USERNAME = "root"  # Account name to login to ChromeOS
CROS_PASSWORD = "test0000"  # Password to login to ChromeOS
CHARGEN_TXT = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
# The result of 'chargen 62 62'
CHARGEN_TXT_LEN = len(CHARGEN_TXT)
CR = "\r"  # Carriage Return
LF = "\n"  # Line Feed
CRLF = CR + LF
FLAG_FILENAME = "/tmp/chargen_testing"
TPM_CMD = (
    "trunks_client --key_create --rsa=2048 --usage=sign"
    " --key_blob=/tmp/blob &> /dev/null"
)
# A ChromeOS TPM command for the cr50 stress
# purpose.
CR50_LOAD_GEN_CMD = f"while [[ -f {FLAG_FILENAME} ]]; do {TPM_CMD}; done &"
# A command line to run TPM_CMD in background
# infinitely.


class ChargenTestError(Exception):
    """Exception for Uart Stress Test Error"""


class UartSerial:
    """Test Object for a single UART serial device

    Attributes:
      UART_DEV_PROFILES
      char_loss_occurrences: Number that character loss happens
      cleanup_cli: Command list to perform before the test exits
      cr50_workload: True if cr50 should be stressed, or False otherwise
      usb_output: True if output should be generated to USB channel
      dev_prof: Dictionary of device profile
      duration: Time to keep chargen running
      eol: Characters to add at the end of input
      logger: object that store the log
      num_ch_exp: Expected number of characters in output
      num_ch_cap: Number of captured characters in output
      test_cli: Command list to run for chargen test
      test_thread: Thread object that captures the UART output
      serial: serial.Serial object
    """

    UART_DEV_PROFILES = (
        # Kernel
        {
            "prompt": "localhost login:",
            "device_type": "AP",
            "prepare_cmd": [
                CROS_USERNAME,  # Login
                CROS_PASSWORD,  # Password
                "dmesg -D",  # Disable console message
                "touch " + FLAG_FILENAME,  # Create a temp file
            ],
            "cleanup_cmd": [
                "\x03",
                "rm -f " + FLAG_FILENAME,  # Remove the temp file
                "dmesg -E",  # Enable console message
                "logout",  # Logout
            ],
            "end_of_input": LF,
        },
        # EC legacy
        {
            "prompt": ">",
            "device_type": "EC(legacy)",
            "prepare_cmd": ["chan save", "chan 0"],  # Disable console message
            "cleanup_cmd": ["", "chan restore"],
            "end_of_input": CRLF,
        },
        # EC Zephyr
        {
            "prompt": "ec:~$",
            "device_type": "EC(Zephyr)",
            "prepare_cmd": ["chan save", "chan 0"],  # Disable console message
            "cleanup_cmd": ["x", "", "chan restore"],
            "end_of_input": CRLF,
        },
    )

    def __init__(
        self,
        port,
        duration,
        timeout=1,
        baudrate=BAUDRATE,
        cr50_workload=False,
        usb_output=False,
    ):
        """Initialize UartSerial

        Args:
          port: UART device path. e.g. /dev/ttyUSB0
          duration: Time to test, in seconds
          timeout: Read timeout value.
          baudrate: Baud rate such as 9600 or 115200.
          cr50_workload: True if a workload should be generated on cr50
          usb_output: True if a workload should be generated to USB channel
        """

        # Initialize serial object
        self.serial = serial.Serial()
        self.serial.port = port
        self.serial.timeout = timeout
        self.serial.baudrate = baudrate

        self.duration = duration
        self.cr50_workload = cr50_workload
        self.usb_output = usb_output

        self.logger = logging.getLogger(type(self).__name__ + "| " + port)
        self.test_thread = threading.Thread(target=self.stress_test_thread)

        self.dev_prof = {}
        self.cleanup_cli = []
        self.test_cli = []
        self.eol = CRLF
        self.num_ch_exp = 0
        self.num_ch_cap = 0
        self.char_loss_occurrences = 0
        atexit.register(self.cleanup)

    def run_command(self, command_lines, delay=0):
        """Run command(s) at UART prompt

        Args:
          command_lines: list of commands to run.
          delay: delay after a command in second
        """
        for cli in command_lines:
            self.logger.debug("run %r", cli)

            self.serial.write((cli + self.eol).encode())
            self.serial.flush()
            if delay:
                time.sleep(delay)

    def cleanup(self):
        """Before termination, clean up the UART device."""
        self.logger.debug("Closing...")

        self.serial.open()
        self.run_command(self.cleanup_cli)  # Run cleanup commands
        self.serial.close()

        self.logger.debug("Cleanup done")

    def get_output(self):
        """Capture the UART output

        Args:
          stop_char: Read output buffer until it reads stop_char.

        Returns:
          text from UART output.
        """
        if self.serial.inWaiting() == 0:
            time.sleep(1)

        return self.serial.read(self.serial.inWaiting()).decode(errors="ignore")

    def prepare(self):
        """Prepare the test:

        Identify the type of UART device (EC or Kernel?), then
        decide what kind of commands to use to generate stress loads.

        Raises:
          ChargenTestError if UART source can't be identified.
        """
        try:
            self.logger.info("Preparing...")

            self.serial.open()

            # Prepare the device for test
            self.serial.flushInput()
            self.serial.flushOutput()

            self.get_output()  # drain data

            # Send a couple of line feeds, and capture the prompt text.
            self.run_command(["", ""], delay=1)
            prompt_txt = self.get_output()

            # Detect the device source: EC or AP?
            # Detect if the device is AP or EC console based on the captured.
            for dev_prof in self.UART_DEV_PROFILES:
                if dev_prof["prompt"] in prompt_txt:
                    self.dev_prof = dev_prof
                    break
            else:
                # No prompt patterns were found. UART seems not responding or in
                # an undesirable status.
                if prompt_txt:
                    raise ChargenTestError(
                        f"{self.serial.port}: Got an unknown prompt text: {prompt_txt}\n"
                        f"Check manually whether {self.serial.port} is available."
                    )
                raise ChargenTestError(
                    f"{self.serial.port}: Got no input. Close any other connections"
                    " to this port, and try it again."
                )

            self.logger.info(
                "Detected as %s UART", self.dev_prof["device_type"]
            )
            # Log displays the UART type (AP|EC) instead of device filename.
            self.logger = logging.getLogger(
                type(self).__name__ + "| " + self.dev_prof["device_type"]
            )

            # Either login to AP or run some commands to prepare the device
            # for test
            self.eol = self.dev_prof["end_of_input"]
            self.run_command(self.dev_prof["prepare_cmd"], delay=2)
            self.cleanup_cli += self.dev_prof["cleanup_cmd"]

            # 'chargen' of AP does not have option for USB output.
            # Force it work on UART.
            if self.dev_prof["device_type"] == "AP":
                self.usb_output = False

            # Check whether the command 'chargen' is available in the device.
            # 'chargen 1 4' is supposed to print '0000'
            self.get_output()  # drain data

            chargen_cmd = "chargen 1 4"
            if self.usb_output:
                chargen_cmd += " usb"
            self.run_command([chargen_cmd], delay=1)
            tmp_txt = self.get_output()

            # Check whether chargen command is available.
            if "0000" not in tmp_txt:
                raise ChargenTestError(
                    f"{self.dev_prof['device_type']}: Chargen got an unexpected result: {tmp_txt}"
                )

            self.num_ch_exp = int(self.serial.baudrate * self.duration / 10)
            chargen_cmd = (
                "chargen " + str(CHARGEN_TXT_LEN) + " " + str(self.num_ch_exp)
            )
            if self.usb_output:
                chargen_cmd += " usb"
            self.test_cli = [chargen_cmd]

            self.logger.info("Ready to test")
        finally:
            self.serial.close()

    def stress_test_thread(self):
        """Test thread

        Raises:
          ChargenTestError: if broken character is found.
        """
        try:
            self.serial.open()
            self.serial.flushInput()
            self.serial.flushOutput()

            # Run TPM command in background to burden cr50.
            if self.dev_prof["device_type"] == "AP" and self.cr50_workload:
                self.run_command([CR50_LOAD_GEN_CMD])
                self.logger.debug("run TPM job while %s exists", FLAG_FILENAME)

            # Run the command 'chargen', one time
            self.run_command([""])  # Give a line feed
            self.get_output()  # Drain the output
            self.run_command(self.test_cli, delay=1)
            self.serial.readline()  # Drain the echoed command line.

            err_msg = "%s: Expected %r but got %s after %d char received"

            # Keep capturing the output until the test timer is expired.
            self.num_ch_cap = 0
            self.char_loss_occurrences = 0
            data_starve_count = 0

            # Expected number of characters in total
            total_num_ch = self.num_ch_exp
            ch_exp = CHARGEN_TXT[0]
            # Any character value is ok for loop initial condition.
            ch_cap = "z"

            while self.num_ch_cap < total_num_ch:
                captured = self.get_output()

                if captured:
                    if self.num_ch_cap == 0:
                        # Strip prompt on first read (if it's there)
                        start = captured.find("0123")
                        if start > 0:
                            captured = captured[start:]
                    # There is some output data. Reset the data starvation count.
                    data_starve_count = 0
                else:
                    data_starve_count += 1
                    if data_starve_count > 1:
                        # If nothing was captured more than once, then terminate the test.
                        self.logger.debug("No more output")
                        break

                for ch_cap in captured:
                    if ch_cap not in CHARGEN_TXT:
                        self.logger.error(
                            "Whole captured characters: %r", captured
                        )
                        raise ChargenTestError(
                            err_msg
                            % (
                                "Broken char captured",
                                ch_exp,
                                hex(ord(ch_cap)),
                                self.num_ch_cap,
                            )
                        )

                    if ch_exp != ch_cap:
                        # If it is alpha-numeric but not continuous, then some characters
                        # are lost.
                        self.logger.error(
                            err_msg,
                            "Char loss detected",
                            ch_exp,
                            repr(ch_cap),
                            self.num_ch_cap,
                        )
                        self.char_loss_occurrences += 1

                        # Recalculate the expected number of characters to adjust
                        # termination condition. The loss might be bigger than this
                        # adjustment, but it is okay since it will terminates by either
                        # CR/LF detection or by data starvation.
                        idx_ch_exp = CHARGEN_TXT.find(ch_exp)
                        idx_ch_cap = CHARGEN_TXT.find(ch_cap)
                        if idx_ch_cap < idx_ch_exp:
                            idx_ch_cap += len(CHARGEN_TXT)
                        total_num_ch -= idx_ch_cap - idx_ch_exp

                    self.num_ch_cap += 1
                    if self.num_ch_cap >= total_num_ch:
                        break

                    # Determine What character is expected next?
                    ch_exp = CHARGEN_TXT[
                        (CHARGEN_TXT.find(ch_cap) + 1) % CHARGEN_TXT_LEN
                    ]

        finally:
            self.serial.close()

    def start_test(self):
        """Start the test thread"""
        self.logger.info("Test thread starts")
        self.test_thread.start()

    def wait_test_done(self):
        """Wait until the test thread get done and join"""
        self.test_thread.join()
        self.logger.info("Test thread is done")

    def get_result(self):
        """Display the result

        Returns:
          Integer = the number of lost character

        Raises:
          ChargenTestError: if the capture is corrupted.
        """
        # If more characters than expected are captured, it means some messages
        # from other than chargen are mixed. Stop processing further.
        if self.num_ch_exp < self.num_ch_cap:
            raise ChargenTestError(
                f"{self.dev_prof['device_type']}: UART output is corrupted."
            )

        # Get the count difference between the expected to the captured
        # as the number of lost character.
        char_lost = self.num_ch_exp - self.num_ch_cap
        self.logger.info(
            "%8d char lost / %10d (%.1f %%)",
            char_lost,
            self.num_ch_exp,
            char_lost * 100.0 / self.num_ch_exp,
        )

        return char_lost, self.num_ch_exp, self.char_loss_occurrences


class ChargenTest:
    """UART stress tester

    Attributes:
      logger: logging object
      serials: Dictionary where key is filename of UART device, and the value is
               UartSerial object
    """

    def __init__(self, ports, duration, cr50_workload=False, usb_output=False):
        """Initialize UART stress tester

        Args:
          ports: List of UART ports to test.
          duration: Time to keep testing in seconds.
          cr50_workload: True if a workload should be generated on cr50
          usb_output: True if a workload should be generated to USB channel

        Raises:
          ChargenTestError: if any of ports is not a valid character device.
        """

        # Save the arguments
        for port in ports:
            try:
                mode = os.stat(port).st_mode
            except OSError as err:
                raise ChargenTestError(err) from err
            if not stat.S_ISCHR(mode):
                raise ChargenTestError(f"{port} is not a character device.")

        if duration <= 0:
            raise ChargenTestError("Input error: duration is not positive.")

        # Initialize logging object
        self.logger = logging.getLogger(type(self).__name__)

        # Create an UartSerial object per UART port
        self.serials = {}  # UartSerial objects
        for port in ports:
            self.serials[port] = UartSerial(
                port=port,
                duration=duration,
                cr50_workload=cr50_workload,
                usb_output=usb_output,
            )

    def prepare(self):
        """Prepare the test for each UART port"""
        self.logger.info("Prepare ports for test")
        for _, ser in self.serials.items():
            ser.prepare()
        self.logger.info("Ports are ready to test")

    def print_result(self):
        """Display the test result for each UART port

        Returns:
          char_lost: Total number of characters lost
        """
        char_lost = 0
        for _, ser in self.serials.items():
            (tmp_lost, _, _) = ser.get_result()
            char_lost += tmp_lost

        # If any characters are lost, then test fails.
        msg = f"lost {char_lost:d} character(s) from the test"
        if char_lost > 0:
            self.logger.error("FAIL: %s", msg)
        else:
            self.logger.info("PASS: %s", msg)

        return char_lost

    def run(self):
        """Run the stress test on UART port(s)

        Raises:
          ChargenTestError: If any characters are lost.
        """

        # Detect UART source type, and decide which command to test.
        self.prepare()

        # Run the test on each UART port in thread.
        self.logger.info("Test starts")
        for _, ser in self.serials.items():
            ser.start_test()

        # Wait all tests to finish.
        for _, ser in self.serials.items():
            ser.wait_test_done()

        # Print the result.
        char_lost = self.print_result()
        if char_lost:
            raise ChargenTestError(
                f"Test failed: lost {char_lost:d} character(s)"
            )

        self.logger.info("Test is done")


def parse_args(cmdline):
    """Parse command line arguments.

    Args:
      cmdline: list to be parsed

    Returns:
      tuple (options, args) where args is a list of cmdline arguments that the
      parser was unable to match i.e. they're servod controls, not options.
    """
    description = """%(prog)s repeats sending a uart console command
to each UART device for a given time, and check if output
has any missing characters.

Examples:
    %(prog)s /dev/ttyUSB2 --time 3600
    %(prog)s /dev/ttyUSB1 /dev/ttyUSB2 --debug
    %(prog)s /dev/ttyUSB1 /dev/ttyUSB2 --cr50
"""

    parser = argparse.ArgumentParser(
        description=description, formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "port", type=str, nargs="*", help="UART device path to test"
    )
    parser.add_argument(
        "-c",
        "--cr50",
        action="store_true",
        default=False,
        help="generate TPM workload on cr50",
    )
    parser.add_argument(
        "-d",
        "--debug",
        action="store_true",
        default=False,
        help="enable debug messages",
    )
    parser.add_argument(
        "-t", "--time", type=int, help="Test duration in second", default=300
    )
    parser.add_argument(
        "-u",
        "--usb",
        action="store_true",
        default=False,
        help="Generate output to USB channel instead",
    )
    return parser.parse_known_args(cmdline)


def main():
    """Main function wrapper"""
    try:
        (options, _) = parse_args(sys.argv[1:])

        # Set Log format
        log_format = "%(asctime)s %(levelname)-6s | %(name)-25s"
        date_format = "%Y-%m-%d %H:%M:%S"
        if options.debug:
            log_format += " | %(filename)s:%(lineno)4d:%(funcName)-18s"
            loglevel = logging.DEBUG
        else:
            loglevel = logging.INFO
        log_format += " | %(message)s"

        logging.basicConfig(
            level=loglevel, format=log_format, datefmt=date_format
        )

        # Create a ChargenTest object
        utest = ChargenTest(
            options.port,
            options.time,
            cr50_workload=options.cr50,
            usb_output=options.usb,
        )
        utest.run()  # Run

    except KeyboardInterrupt:
        sys.exit(0)

    except ChargenTestError as err:
        logging.error(str(err))
        sys.exit(1)


if __name__ == "__main__":
    main()
