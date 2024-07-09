#!/usr/bin/env python3
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A script which builds, flashes, and runs EC CTS

Software prerequisites:
- openocd version 0.10 or above
- lsusb
- udevadm

To try it out, hook two boards (DEFAULT_TH and DEFAULT_DUT) with USB cables
to the host and execute the script:
  $ ./cts.py
It'll run mock tests. The result will be stored in CTS_TEST_RESULT_DIR.
"""

import argparse
import os
import shutil
import time

from common import board


CTS_RC_PREFIX = "CTS_RC_"
DEFAULT_TH = "stm32l476g-eval"
DEFAULT_DUT = "nucleo-f072rb"
MAX_SUITE_TIME_SEC = 5
CTS_TEST_RESULT_DIR = "/tmp/ects"

# Host only return codes. Make sure they match values in cts.rc
CTS_RC_DID_NOT_START = -1  # test did not run.
CTS_RC_DID_NOT_END = -2  # test did not run.
CTS_RC_DUPLICATE_RUN = -3  # test was run multiple times.
CTS_RC_INVALID_RETURN_CODE = -4  # failed to parse return code


class Cts:
    """Class that represents a eCTS run.

    Attributes:
      dut: DeviceUnderTest object representing DUT
      test_harness: TestHarness object representing a test harness
      module: Name of module to build/run tests for
      testlist: List of strings of test names contained in given module
      return_codes: Dict of strings of return codes, with a code's integer
        value being the index for the corresponding string representation
    """

    def __init__(self, ec_dir, test_harness, dut, module):
        """Initializes cts class object with given arguments.

        Args:
          ec_dir: Path to ec directory
          th: Name of the test harness board
          dut: Name of the device under test board
          module: Name of module to build/run tests for (e.g. gpio, interrupt)
        """
        self.results_dir = os.path.join(CTS_TEST_RESULT_DIR, dut, module)
        if os.path.isdir(self.results_dir):
            shutil.rmtree(self.results_dir)
        else:
            os.makedirs(self.results_dir)
        self.ec_dir = ec_dir
        self.module = module
        serial_path = os.path.join(CTS_TEST_RESULT_DIR, "th_serial")
        self.test_harness = board.TestHarness(
            test_harness, module, self.results_dir, serial_path
        )
        self.dut = board.DeviceUnderTest(
            dut, self.test_harness, module, self.results_dir
        )
        cts_dir = os.path.join(self.ec_dir, "cts")
        testlist_path = os.path.join(cts_dir, self.module, "cts.testlist")
        return_codes_path = os.path.join(cts_dir, "common", "cts.rc")
        self.get_return_codes(return_codes_path)
        self.testlist = self.get_macro_args(testlist_path, "CTS_TEST")
        self.formatted_results = ""

    def build(self):
        """Build images for DUT and TH."""
        print("Building DUT image...")
        if not self.dut.build(self.ec_dir):
            raise RuntimeError(f"Building module {self.module} for DUT failed")
        print("Building TH image...")
        if not self.test_harness.build(self.ec_dir):
            raise RuntimeError(f"Building module {self.module} for TH failed")

    def flash_boards(self):
        """Flashes TH and DUT with their most recently built ec.bin."""
        cts_module = "cts_" + self.module
        image_path = os.path.join(
            "build", self.test_harness.board, cts_module, "ec.bin"
        )
        self.identify_boards()
        print("Flashing TH with", image_path)
        if not self.test_harness.flash(image_path):
            raise RuntimeError("Flashing TH failed")
        image_path = os.path.join("build", self.dut.board, cts_module, "ec.bin")
        print("Flashing DUT with", image_path)
        if not self.dut.flash(image_path):
            raise RuntimeError("Flashing DUT failed")

    def setup(self):
        """Setup boards."""
        self.test_harness.save_serial()

    def identify_boards(self):
        """Updates serials of TH and DUT in that order (order matters)."""
        self.test_harness.get_serial()
        self.dut.get_serial()

    def get_macro_args(self, filepath, macro):
        """Get list of args of a macro in a file when macro.

        Args:
          filepath: String containing absolute path to the file
          macro: String containing text of macro to get args of

        Returns:
          List of dictionaries where each entry is:
            'name': Test name,
            'th_string': Expected string from TH,
            'dut_string': Expected string from DUT,
        """
        tests = []
        with open(filepath, "r", encoding="utf-8") as macro_file:
            lines = macro_file.readlines()
            joined = "".join(lines).replace("\\\n", "").splitlines()
            for line in joined:
                if not line.strip().startswith(macro):
                    continue
                entry = {}
                line = line.strip()[len(macro) :]
                line = line.strip("()").split(",")
                entry["name"] = line[0].strip()
                entry["th_rc"] = self.get_return_code_value(
                    line[1].strip().strip('"')
                )
                entry["th_string"] = line[2].strip().strip('"')
                entry["dut_rc"] = self.get_return_code_value(
                    line[3].strip().strip('"')
                )
                entry["dut_string"] = line[4].strip().strip('"')
                tests.append(entry)
        return tests

    def get_return_codes(self, filepath):
        """Read return code names from the return code definition file."""
        self.return_codes = {}
        val = 0
        with open(filepath, "r", encoding="utf-8") as input_file:
            for line in input_file:
                line = line.strip()
                if not line.startswith(CTS_RC_PREFIX):
                    continue
                line = line.split(",")[0]
                if "=" in line:
                    tokens = line.split("=")
                    line = tokens[0].strip()
                    val = int(tokens[1].strip())
                self.return_codes[line] = val
                val += 1

    def parse_output(self, output):
        """Parse console output from DUT or TH.

        Args:
          output: String containing consoule output

        Returns:
          List of dictionaries where each key and value are:
            name = 'ects_test_x',
            started = True/False,
            ended = True/False,
            rc = CTS_RC_*,
            output = All text between 'ects_test_x start' and 'ects_test_x end'
        """
        results = []
        i = 0
        for test in self.testlist:
            results.append({})
            results[i]["name"] = test["name"]
            results[i]["started"] = False
            results[i]["rc"] = CTS_RC_DID_NOT_START
            results[i]["string"] = False
            results[i]["output"] = []
            i += 1

        i = 0
        for line in [ln.strip() for ln in output.split("\n")]:
            if i + 1 > len(results):
                break
            tokens = line.split()
            if len(tokens) >= 2:
                if tokens[0].strip() == results[i]["name"]:
                    if tokens[1].strip() == "start":
                        # start line found
                        if results[i]["started"]:  # Already started
                            results[i]["rc"] = CTS_RC_DUPLICATE_RUN
                        else:
                            results[i]["rc"] = CTS_RC_DID_NOT_END
                            results[i]["started"] = True
                        continue
                    if results[i]["started"] and tokens[1].strip() == "end":
                        # end line found
                        results[i]["rc"] = CTS_RC_INVALID_RETURN_CODE
                        if len(tokens) == 3:
                            try:
                                results[i]["rc"] = int(tokens[2].strip())
                            except ValueError:
                                pass
                        # Since index is incremented when 'end' is encountered, we don't
                        # need to check duplicate 'end'.
                        i += 1
                        continue
            if results[i]["started"]:
                results[i]["output"].append(line)

        return results

    def get_return_code_name(self, code, strip_prefix=False):
        """Converts a return code into a name."""
        name = ""
        for k, val in self.return_codes.items():
            if val == code:
                if strip_prefix:
                    name = k[len(CTS_RC_PREFIX) :]
                else:
                    name = k
        return name

    def get_return_code_value(self, name):
        """Converts a return name into a code."""
        if name:
            return self.return_codes[name]
        return 0

    def evaluate_run(self, dut_output, th_output):
        """Parse outputs to derive test results.

        Args:
          dut_output: String output of DUT
          th_output: String output of TH

        Returns:
          th_results: list of test results for TH
          dut_results: list of test results for DUT
        """
        th_results = self.parse_output(th_output)
        dut_results = self.parse_output(dut_output)

        # Search for expected string in each output
        for i, val in enumerate(self.testlist):
            if (
                val["th_string"] in th_results[i]["output"]
                or not val["th_string"]
            ):
                th_results[i]["string"] = True
            if (
                val["dut_string"] in dut_results[i]["output"]
                or not val["dut_string"]
            ):
                dut_results[i]["string"] = True

        return th_results, dut_results

    def print_result(self, th_results, dut_results):
        """Print results to the screen.

        Args:
          th_results: list of test results for TH
          dut_results: list of test results for DUT
        """
        len_test_name = max(len(s["name"]) for s in self.testlist)
        len_code_name = max(
            len(self.get_return_code_name(v, True))
            for v in self.return_codes.values()
        )

        head = "{:^" + str(len_test_name) + "} "
        head += "{:^" + str(len_code_name) + "} "
        head += "{:^" + str(len_code_name) + "}"
        head += "{:^" + str(len(" TH_STR")) + "}"
        head += "{:^" + str(len(" DUT_STR")) + "}"
        head += "{:^" + str(len(" RESULT")) + "}\n"
        fmt = "{:" + str(len_test_name) + "} "
        fmt += "{:>" + str(len_code_name) + "} "
        fmt += "{:>" + str(len_code_name) + "}"
        fmt += "{:>" + str(len(" TH_STR")) + "}"
        fmt += "{:>" + str(len(" DUT_STR")) + "}"
        fmt += "{:>" + str(len(" RESULT")) + "}\n"

        self.formatted_results = head.format(
            "TEST NAME", "TH_RC", "DUT_RC", " TH_STR", " DUT_STR", " RESULT"
        )
        for i, ddd in enumerate(dut_results):
            th_cn = self.get_return_code_name(th_results[i]["rc"], True)
            dut_cn = self.get_return_code_name(dut_results[i]["rc"], True)
            th_res = self.evaluate_result(
                th_results[i],
                self.testlist[i]["th_rc"],
                self.testlist[i]["th_string"],
            )
            dut_res = self.evaluate_result(
                dut_results[i],
                self.testlist[i]["dut_rc"],
                self.testlist[i]["dut_string"],
            )
            self.formatted_results += fmt.format(
                ddd["name"],
                th_cn,
                dut_cn,
                "YES" if th_results[i]["string"] else "NO",
                "YES" if dut_results[i]["string"] else "NO",
                "PASS" if th_res and dut_res else "FAIL",
            )

    @staticmethod
    def evaluate_result(result, expected_rc, expected_string):
        """Converts a result string to boolean."""
        if result["rc"] != expected_rc:
            return False
        if expected_string and expected_string not in result["output"]:
            return False
        return True

    def run(self):
        """Resets boards, records test results in results dir."""
        print("Reading serials...")
        self.identify_boards()
        print("Opening DUT tty...")
        self.dut.setup_tty()
        print("Opening TH tty...")
        self.test_harness.setup_tty()

        # Boards might be still writing to tty. Wait a few seconds before flashing.
        time.sleep(3)

        # clear buffers
        print("Clearing DUT tty...")
        self.dut.read_tty()
        print("Clearing TH tty...")
        self.test_harness.read_tty()

        # Resets the boards and allows them to run tests
        # Due to current (7/27/16) version of sync function,
        # both boards must be rest and halted, with the th
        # resuming first, in order for the test suite to run in sync
        print("Halting TH...")
        if not self.test_harness.reset_halt():
            raise RuntimeError("Failed to halt TH")
        print("Halting DUT...")
        if not self.dut.reset_halt():
            raise RuntimeError("Failed to halt DUT")
        print("Resuming TH...")
        if not self.test_harness.resume():
            raise RuntimeError("Failed to resume TH")
        print("Resuming DUT...")
        if not self.dut.resume():
            raise RuntimeError("Failed to resume DUT")

        time.sleep(MAX_SUITE_TIME_SEC)

        print("Reading DUT tty...")
        dut_output, _ = self.dut.read_tty()
        self.dut.close_tty()
        print("Reading TH tty...")
        th_output, _ = self.test_harness.read_tty()
        self.test_harness.close_tty()

        print("Halting TH...")
        if not self.test_harness.reset_halt():
            raise RuntimeError("Failed to halt TH")
        print("Halting DUT...")
        if not self.dut.reset_halt():
            raise RuntimeError("Failed to halt DUT")

        if not dut_output or not th_output:
            raise ValueError(
                "Output missing from boards. If you have a process "
                "reading ttyACMx, please kill that process and try "
                "again."
            )

        print("Pursing results...")
        th_results, dut_results = self.evaluate_run(dut_output, th_output)

        # Print out results
        self.print_result(th_results, dut_results)

        # Write results
        dest = os.path.join(self.results_dir, "results.log")
        with open(dest, "w", encoding="utf-8") as rfile:
            rfile.write(self.formatted_results)

        # Write UART outputs
        dest = os.path.join(self.results_dir, "uart_th.log")
        with open(dest, "w", encoding="utf-8") as rfile:
            rfile.write(th_output)
        dest = os.path.join(self.results_dir, "uart_dut.log")
        with open(dest, "w", encoding="utf-8") as rfile:
            rfile.write(dut_output)

        print(self.formatted_results)

        # TODO(chromium:735652): Should set exit code for the shell


def main():
    """Main function."""
    ec_dir = os.path.realpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    )
    os.chdir(ec_dir)

    dut = DEFAULT_DUT
    module = "meta"

    parser = argparse.ArgumentParser(description="Used to build/flash boards")
    parser.add_argument(
        "-d", "--dut", help="Specify DUT you want to build/flash"
    )
    parser.add_argument(
        "-m", "--module", help="Specify module you want to build/flash"
    )
    parser.add_argument(
        "-s",
        "--setup",
        action="store_true",
        help="Connect only the TH to save its serial",
    )
    parser.add_argument(
        "-b",
        "--build",
        action="store_true",
        help="Build test suite (no flashing)",
    )
    parser.add_argument(
        "-f",
        "--flash",
        action="store_true",
        help="Flash boards with most recent images",
    )
    parser.add_argument(
        "-r", "--run", action="store_true", help="Run tests without flashing"
    )

    args = parser.parse_args()

    if args.module:
        module = args.module

    if args.dut:
        dut = args.dut

    cts = Cts(ec_dir, DEFAULT_TH, dut=dut, module=module)

    if args.setup:
        cts.setup()
    elif args.build:
        cts.build()
    elif args.flash:
        cts.flash_boards()
    elif args.run:
        cts.run()
    else:
        cts.build()
        cts.flash_boards()
        cts.run()


if __name__ == "__main__":
    main()
