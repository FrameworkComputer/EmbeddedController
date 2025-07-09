#!/usr/bin/env -S python3 -u
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Run unit tests on Renode emulator.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import os
from pathlib import Path
import subprocess
import sys

import firmware_builder_lib

# pylint: disable=import-error
from google.protobuf import json_format

from chromite.api.gen_sdk.chromite.api import firmware_pb2


EC_BOARDS = [
    "bloonchipper",
    "dartmonkey",
    "helipilot",
]

ZEPHYR_BOARDS = [
    "bloonchipper",
]


def build(opts):
    """Build all the EC unit tests."""

    working_dir = Path(__file__).parents[2].resolve()
    cmd = [
        "make",
        f"-j{opts.cpus}",
    ]
    cmd.extend(["tests-" + b for b in EC_BOARDS])
    subprocess.run(cmd, cwd=working_dir, check=True)


def bundle(opts):
    """No-op."""

    # We don't produce any artifacts, but the info file is expected, so create
    # an empty one.
    with open(opts.metadata, "w", encoding="utf-8") as file:
        file.write(
            json_format.MessageToJson(
                firmware_pb2.FirmwareArtifactInfo()  # pylint: disable=no-member
            )
        )


def run_device_tests(board: str, working_dir: Path, zephyr: bool):
    """Run device tests on Renode emulator."""
    cmd = [
        "test/run_device_tests.py",
        "-b",
        board,
        "--renode",
        "--with_private",
        "no",
    ]

    if zephyr:
        cmd.append("--zephyr")

    subprocess.run(
        cmd,
        cwd=working_dir,
        check=True,
    )


def test(_opts):
    """Runs EC unit tests with Renode."""

    working_dir = Path(__file__).parents[2].resolve()
    renode_install_dir = working_dir.joinpath("renode")

    # Renode is built as a subtool and available versions can be found here:
    # https://chrome-infra-packages.appspot.com/p/chromiumos/infra/tools/renode.
    cipd_renode_version = (
        "ebuild_source:"
        + "app-emulation/renode-1.15.3_p20241207,"
        + "dev-libs/icu-70.1-r2,"
        + "dev-libs/openssl-3.2.1-r1,"
        + "dev-libs/userspace-rcu-0.13.2-r1,"
        + "dev-util/lttng-ust-2.12.1-r1"
    )

    # Install Renode.
    subprocess.run(
        [
            "cipd",
            "ensure",
            "-ensure-file",
            "-",
            "-root",
            renode_install_dir,
        ],
        input=("chromiumos/infra/tools/renode " + cipd_renode_version).encode(
            "utf-8"
        ),
        cwd=working_dir,
        check=True,
    )

    os.environ["PATH"] += ":" + str(renode_install_dir.joinpath("bin"))

    # Run unit tests with Renode.
    # TODO(b/371633141): Add a parallel option to run_device_tests.py to speed
    # this up. Right now the EC/Zephyr coverage builders take longer than this,
    # so it doesn't affect overall CQ time.
    for board in EC_BOARDS:
        run_device_tests(board, working_dir, zephyr=False)

    for board in ZEPHYR_BOARDS:
        run_device_tests(board, working_dir, zephyr=True)


def main(args):
    """Builds, bundles, or tests.

    Additionally, the tool reports build metrics.
    """
    parser, _ = firmware_builder_lib.create_arg_parser(build, bundle, test)

    opts = parser.parse_args(args)

    if not hasattr(opts, "func"):
        print("Must select a valid sub command!")
        return -1

    # Run selected sub command function
    try:
        opts.func(opts)
    except subprocess.CalledProcessError:
        ec_dir = os.path.dirname(__file__)
        failed_dir = os.path.join(ec_dir, ".failedboards")
        if os.path.isdir(failed_dir):
            print("Failed boards/tests:")
            for fail in os.listdir(failed_dir):
                print(f"\t{fail}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
