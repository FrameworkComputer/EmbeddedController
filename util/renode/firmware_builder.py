#!/usr/bin/env -S python3 -u
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Run unit tests on Renode emulator.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import argparse
import multiprocessing
import os
from pathlib import Path
import subprocess
import sys

# pylint: disable=import-error
from google.protobuf import json_format

from chromite.api.gen_sdk.chromite.api import firmware_pb2


BOARDS = [
    "bloonchipper",
    "dartmonkey",
]


def build(opts):
    """Build all the EC unit tests."""

    working_dir = Path(__file__).parents[2].resolve()
    cmd = [
        "make",
        f"-j{opts.cpus}",
    ]
    cmd.extend(["tests-" + b for b in BOARDS])
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


def test(_opts):
    """Runs EC unit tests with Renode."""

    working_dir = Path(__file__).parents[2].resolve()
    renode_install_dir = working_dir.joinpath("renode")

    # Renode is built as a subtool and available versions can be found here:
    # https://chrome-infra-packages.appspot.com/p/chromiumos/infra/tools/renode.
    cipd_renode_version = (
        "ebuild_source:"
        + "app-crypt/mit-krb5-1.21.3,"
        + "app-emulation/renode-1.15.3_p20241010,"
        + "dev-lang/mono-6.12.0.122,"
        + "sys-fs/e2fsprogs-1.47.0-r4,"
        + "sys-libs/glibc-2.37-r9"
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
    for board in BOARDS:
        subprocess.run(
            [
                "test/run_device_tests.py",
                "-b",
                board,
                "--renode",
                "--with_private",
                "no",
            ],
            cwd=working_dir,
            check=True,
        )


def main(args):
    """Builds, bundles, or tests.

    Additionally, the tool reports build metrics.
    """
    opts = parse_args(args)

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
    else:
        return 0


def parse_args(args):
    """Parse all command line args and return opts dict."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--cpus",
        default=multiprocessing.cpu_count(),
        help="The number of cores to use.",
    )

    parser.add_argument(
        "--metrics",
        dest="metrics",
        required=True,
        help="File to write the json-encoded MetricsList proto message.",
    )

    parser.add_argument(
        "--metadata",
        required=False,
        help="Full pathname for the file in which to write build artifact metadata.",
    )

    parser.add_argument(
        "--output-dir",
        required=False,
        help="Full pathanme for the directory in which to bundle build artifacts.",
    )

    parser.add_argument(
        "--code-coverage",
        required=False,
        action="store_true",
        help="Build host-based unit tests for code coverage.",
    )

    parser.add_argument(
        "--bcs-version",
        dest="bcs_version",
        default="",
        required=False,
        # TODO(b/180008931): make this required=True.
        help="BCS version to include in metadata.",
    )

    # Would make this required=True, but not available until 3.7
    sub_cmds = parser.add_subparsers()

    build_cmd = sub_cmds.add_parser("build", help="Builds all firmware targets")
    build_cmd.set_defaults(func=build)

    build_cmd = sub_cmds.add_parser(
        "bundle",
        help="Creates a tarball containing build artifacts from all firmware targets",
    )
    build_cmd.set_defaults(func=bundle)

    test_cmd = sub_cmds.add_parser("test", help="Runs all firmware unit tests")
    test_cmd.set_defaults(func=test)

    return parser.parse_args(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
