#!/usr/bin/env -S python3 -u
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Run unit tests on Renode emulator.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import argparse
import getpass
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Dict, List, NamedTuple, Optional

import firmware_builder_lib

# pylint: disable=import-error
from google.protobuf import json_format

from chromite.api.gen_sdk.chromite.api import firmware_pb2


ZEPHYR_BOARDS = [
    "bloonchipper",
    "chudow",
    "helipilot",
    "sanok",
]


BoardName = str
ResultPath = Path


class RenodeRunResult(NamedTuple):
    """Result of the Renode test execution."""

    success: bool
    results: Dict[BoardName, ResultPath]


def build(_opts: argparse.Namespace) -> int:
    """No-op."""

    return 0


def bundle(opts: argparse.Namespace) -> int:
    """No-op."""

    # We don't produce any artifacts, but the info file is expected, so create
    # an empty one.
    with open(opts.metadata, "w", encoding="utf-8") as file:
        file.write(
            json_format.MessageToJson(
                firmware_pb2.FirmwareArtifactInfo()  # pylint: disable=no-member
            )
        )

    return 0


def run_device_tests(
    board: str,
    working_dir: Path,
    zephyr: bool,
    output_file: Optional[Path] = None,
    check: bool = True,
) -> subprocess.CompletedProcess:
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

    if output_file:
        cmd.extend(["--json", str(output_file)])

    return subprocess.run(
        cmd,
        cwd=working_dir,
        check=check,
    )


def _run_renode_tests(
    output_dir: Path, working_dir: Path, boards: List[BoardName]
) -> RenodeRunResult:
    """Runs Renode tests for all boards."""
    success = True
    results: Dict[BoardName, ResultPath] = {}

    for board in boards:
        result_file = output_dir / f"{board}_results.json"
        proc = run_device_tests(
            board,
            working_dir,
            zephyr=True,
            output_file=result_file,
            check=False,
        )
        if proc.returncode != 0:
            success = False
        results[board] = result_file

    return RenodeRunResult(success, results)


def _process_board_results(
    board: BoardName, result_file: ResultPath
) -> List[Dict[str, Any]]:
    """Reads and parses results for a single board."""
    try:
        with result_file.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return []
    except json.JSONDecodeError:
        print(f"Failed to decode JSON from {result_file}", file=sys.stderr)
        return []

    if not isinstance(data, dict):
        print(
            f"Warning: Expected JSON object in {result_file}, got {type(data).__name__}",
            file=sys.stderr,
        )
        return []

    tests = data.get("tests", [])
    if not isinstance(tests, list):
        print(
            f"Warning: Expected 'tests' list in {result_file}, got {type(tests).__name__}",
            file=sys.stderr,
        )
        return []

    valid_tests = []
    for test_case in tests:
        if isinstance(test_case, dict) and "test_name" in test_case:
            # Prepend board name to test name to make it unique
            test_case["test_name"] = f"{board}.{test_case['test_name']}"
            valid_tests.append(test_case)
        else:
            print(
                f"Warning: Unexpected test format in {result_file}: {test_case}",
                file=sys.stderr,
            )
    return valid_tests


def _aggregate_results(results: Dict[BoardName, ResultPath]) -> Dict[str, Any]:
    """Aggregates results from individual board test runs."""
    all_tests = []
    for board, result_file in results.items():
        all_tests.extend(_process_board_results(board, result_file))

    return {"tests": all_tests}


def _write_results(final_results: Dict[str, Any], output_path: Path) -> None:
    """Writes the aggregated test results to a file."""
    with output_path.open("w", encoding="utf-8") as f:
        json.dump(final_results, f, indent=2)

    print(f"Test results written to {output_path}")


def test(opts: argparse.Namespace) -> int:
    """Runs EC unit tests with Renode."""

    working_dir = Path(__file__).parents[2].resolve()
    renode_install_dir = working_dir.joinpath("renode")

    # Renode is built as a subtool and available versions can be found here:
    # https://chrome-infra-packages.appspot.com/p/chromiumos/infra/tools/renode.
    cipd_renode_version = (
        "ebuild_source:"
        + "app-emulation/renode-1.16.1_p20260319,"
        + "dev-libs/icu-70.1-r3,"
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

    # Fix the ~/.config dir if necessary b/431869968 b/310667234
    config_dir = Path.home() / ".config"
    if config_dir.exists() and config_dir.owner() == "root":
        print("Fixing ownership of ~/.config")
        subprocess.run(
            [
                "sudo",
                "chown",
                getpass.getuser(),
                str(config_dir),
            ],
            check=True,
        )

    os.environ["PATH"] += ":" + str(renode_install_dir.joinpath("bin"))

    print("Renode version:")
    subprocess.run(["renode", "--version"], check=True)

    # Run unit tests with Renode.
    # TODO(b/371633141): Add a parallel option to run_device_tests.py to speed
    # this up. Right now the EC/Zephyr coverage builders take longer than this,
    # so it doesn't affect overall CQ time.
    output_dir_val = getattr(opts, "output_dir", None)
    output_dir = Path(output_dir_val) if output_dir_val else working_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    run_result = _run_renode_tests(output_dir, working_dir, ZEPHYR_BOARDS)
    final_results = _aggregate_results(run_result.results)

    output_path = output_dir / "test_results.json"
    _write_results(final_results, output_path)

    if not run_result.success:
        raise RuntimeError("One or more renode tests failed")

    return 0


def main(args: list[str]) -> int:
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
    except (subprocess.CalledProcessError, RuntimeError):
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
