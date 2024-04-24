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

import sys
from typing import List, Optional

import preupload.lib
import yaml  # pylint: disable=import-error


BLOCKED_FIELDS = {
    "CONF_FILE": "extra_conf_files",
    "OVERLAY_CONFIG": "extra_overlay_confs",
    "DTC_OVERLAY_FILE": "extra_dtc_overlay_files",
}


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Look at all testcase.yaml files passed in on commandline for invalid fields."""
    return_code = 0
    args = preupload.lib.parse_args(argv)
    for filename in args.filename:
        if filename.name == "testcase.yaml":
            lines = preupload.lib.cat_file(args, filename)
            data = yaml.load(lines, Loader=yaml.SafeLoader)

            def scan_section(section, filename):
                nonlocal return_code
                if "extra_args" in section:
                    for field, replacement in BLOCKED_FIELDS.items():
                        if field in section["extra_args"]:
                            print(
                                f"error: Don't specify {field} in "
                                f"`extra_args`. Use `{replacement}`: "
                                f"{filename}",
                                file=sys.stderr,
                            )
                            return_code = 1

            if "common" in data:
                scan_section(data["common"], filename)
            if "tests" in data:
                for section in data["tests"]:
                    scan_section(data["tests"][section], filename)
            continue

    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
