# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Run all eCTS tests and publish results."""

import argparse
import logging
import os
import subprocess
import sys


# List of tests to run.
TESTS = ["meta", "gpio", "hook", "i2c", "interrupt", "mutex", "task", "timer"]


# pylint:disable=missing-function-docstring,no-self-use
class CtsRunner:
    """Class running eCTS tests."""

    def __init__(self, ec_dir, dryrun):
        self.ec_dir = ec_dir
        self.cts_py = []
        if dryrun:
            self.cts_py += ["echo"]
        self.cts_py += [os.path.join(ec_dir, "cts/cts.py")]

    def run_cmd(self, cmd):
        try:
            ret = subprocess.call(cmd)
            if ret != 0:
                return False
        except OSError:
            return False
        return True

    def run_test(self, test):
        cmd = self.cts_py + ["-m", test]
        self.run_cmd(cmd)

    def run(self, tests):
        for test in tests:
            logging.info("Running %s test.", test)
            self.run_test(test)

    def sync(self):
        logging.info("Syncing tree...")
        os.chdir(self.ec_dir)
        cmd = ["repo", "sync", "."]
        return self.run_cmd(cmd)

    def upload(self):
        logging.info("Uploading results...")


def main():
    if not os.path.exists("/etc/cros_chroot_version"):
        logging.error("This script has to run inside chroot.")
        sys.exit(-1)

    ec_dir = os.path.realpath(os.path.dirname(__file__) + "/..")

    parser = argparse.ArgumentParser(description="Run eCTS and report results.")
    parser.add_argument(
        "-d",
        "--dryrun",
        action="store_true",
        help="Echo commands to be executed without running them.",
    )
    parser.add_argument(
        "-s",
        "--sync",
        action="store_true",
        help="Sync tree before running tests.",
    )
    parser.add_argument(
        "-u", "--upload", action="store_true", help="Upload test results."
    )
    args = parser.parse_args()

    runner = CtsRunner(ec_dir, args.dryrun)

    if args.sync:
        if not runner.sync():
            logging.error("Failed to sync.")
            sys.exit(-1)

    runner.run(TESTS)

    if args.upload:
        runner.upload()


if __name__ == "__main__":
    main()
