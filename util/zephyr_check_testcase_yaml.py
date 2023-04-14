#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Perform pre-submit checks on testcase.yaml files"""

# [VPYTHON:BEGIN]
# python_version: "3.8"
# wheel: <
#   name: "infra/python/wheels/pyyaml-py3"
#   version: "version:5.3.1"
# >
# [VPYTHON:END]

import argparse
import glob
from pathlib import Path
import sys
from typing import List

from twister_launcher import EC_TEST_PATHS
import yaml  # pylint: disable=import-error


def check_extra_args(filepath: Path):
    """Check if extra_args references blocked fields"""
    errors = []

    blocked_fields = {
        "CONF_FILE": "extra_conf_files",
        "OVERLAY_CONFIG": "extra_overlay_confs",
        "DTC_OVERLAY_FILE": "extra_dtc_overlay_files",
    }

    with open(filepath, "r") as file:
        data = yaml.load(file, Loader=yaml.SafeLoader)

    def scan_section(section):
        if "extra_args" in section:
            for field in blocked_fields:
                if field in section["extra_args"]:
                    errors.append(
                        f" * Don't specify {field} in `extra_args`. "
                        f"Use `{blocked_fields[field]}` ({filepath})"
                    )

    if "common" in data:
        scan_section(data["common"])
    if "tests" in data:
        for section in data["tests"]:
            scan_section(data["tests"][section])

    return errors


def validate_files(files: List[Path]) -> List[str]:
    """Run checks on a list of file paths."""
    errors = []

    checkers = [check_extra_args]

    for file in files:
        if not file.name == "testcase.yaml":
            continue
        for check in checkers:
            errors.extend(check(file))

    return errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="List of files to validate. If blank, scan entire EC repo. "
        "If '-', read a newline-separated list of files from stdin",
    )

    args = parser.parse_args()

    if len(args.files) == 0:
        ec_dir = Path(__file__).resolve().parent.parent
        file_list = []

        for p in EC_TEST_PATHS:
            file_list.extend(
                [
                    Path(f)
                    for f in glob.glob(
                        str(ec_dir / p / "**/testcase.yaml"),
                    )
                ]
            )

    elif args.files[0] == Path("-"):
        # Read from stdin
        file_list = [Path(line.strip()) for line in sys.stdin.readlines()]
        file_list.extend(args.files[1:])
    else:
        file_list = args.files

    all_errors = validate_files(file_list)

    if all_errors:
        for error in all_errors:
            sys.stderr.write(error)
            sys.stderr.write("\n")
        sys.exit(1)
