#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper that runs a host test. Handles timeout and stopping the emulator."""

import argparse
import enum
import io
import os
import pathlib
import select
import subprocess
import sys
import time


class TestResult(enum.Enum):
    """An Enum representing the result of running a test."""

    SUCCESS = 0
    FAIL = 1
    TIMEOUT = 2
    UNEXPECTED_TERMINATION = 3

    @property
    def reason(self):
        """Return a map of test result enums to descriptions."""
        return {
            TestResult.SUCCESS: "passed",
            TestResult.FAIL: "failed",
            TestResult.TIMEOUT: "timed out",
            TestResult.UNEXPECTED_TERMINATION: "terminated unexpectedly",
        }[self]


def run_test(path, timeout=10):
    """Runs a test."""
    start_time = time.monotonic()
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "log_path=stderr"

    with subprocess.Popen(
        [path],
        bufsize=0,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        env=env,
        encoding="utf-8",
        errors="replace",
    ) as proc:
        # Put the output pipe in non-blocking mode. We will then select(2)
        # on the pipe to know when we have bytes to process.
        os.set_blocking(proc.stdout.fileno(), False)

        try:
            output_buffer = io.StringIO()
            while True:
                select_timeout = timeout - (time.monotonic() - start_time)
                if select_timeout <= 0:
                    return TestResult.TIMEOUT, output_buffer.getvalue()

                readable, _, _ = select.select(
                    [proc.stdout], [], [], select_timeout
                )

                if not readable:
                    # Indicates that select(2) timed out.
                    return TestResult.TIMEOUT, output_buffer.getvalue()

                output_buffer.write(proc.stdout.read())
                output_log = output_buffer.getvalue()

                if "Pass!" in output_log:
                    return TestResult.SUCCESS, output_log
                if "Fail!" in output_log:
                    return TestResult.FAIL, output_log
                if proc.poll():
                    return TestResult.UNEXPECTED_TERMINATION, output_log
        finally:
            # Check if the process has exited. If not, send it a SIGTERM, wait
            # for it to exit, and if it times out, kill the process directly.
            if not proc.poll():
                try:
                    proc.terminate()
                    proc.wait(timeout)
                except subprocess.TimeoutExpired:
                    proc.kill()


def parse_options(argv):
    """Parse command line flags."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=120,
        help="Timeout to kill test after.",
    )
    parser.add_argument(
        "--coverage",
        action="store_const",
        const="coverage",
        default="host",
        dest="test_target",
        help="Flag if this is a code coverage test.",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Dump emulator output always, even if successful.",
    )
    parser.add_argument("test_name", type=str)
    return parser.parse_args(argv)


def main(argv):
    """The main function."""
    opts = parse_options(argv)

    # Tests will be located in build/host, unless the --coverage flag was
    # provided, in which case they will be in build/coverage.
    exec_path = pathlib.Path(
        "build", opts.test_target, opts.test_name, f"{opts.test_name}.exe"
    )
    if not exec_path.is_file():
        print(f"No test named {opts.test_name} exists!")
        return 1

    # Host tests that use persistence.c leave these files laying around.
    for persist in pathlib.Path("/dev/shm").glob(
        f"EC_persist_*_{opts.test_name}.exe_*"
    ):
        persist.unlink()

    start_time = time.monotonic()
    result, output = run_test(exec_path, timeout=opts.timeout)
    elapsed_time = time.monotonic() - start_time

    print(
        f"{opts.test_name} {result.reason}! ({elapsed_time:.3f} seconds)",
        file=sys.stderr,
    )

    if result is not TestResult.SUCCESS or opts.verbose:
        print("====== Emulator output ======", file=sys.stderr)
        print(output, file=sys.stderr)
        print("=============================", file=sys.stderr)
    return result.value


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
