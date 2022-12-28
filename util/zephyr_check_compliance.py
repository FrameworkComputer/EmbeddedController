#!/usr/bin/env vpython3

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Check a single commit using the Zephyr check_compliance.py script."""

# [VPYTHON:BEGIN]
# python_version: "3.8"
# wheel: <
#   name: "infra/python/wheels/junitparser-py2_py3"
#   version: "version:2.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/future-py2_py3"
#   version: "version:0.18.2"
# >
# wheel: <
#   name: "infra/python/wheels/python-magic-py2_py3"
#   version: "version:0.4.24"
# >
# wheel: <
#   name: "infra/python/wheels/pyyaml-py3"
#   version: "version:5.3.1"
# >
# wheel: <
#   name: "infra/python/wheels/yamllint-py3"
#   version: "version:1.29.0"
# >
# wheel: <
#   name: "infra/python/wheels/pathspec-py3"
#   version: "version:0.9.0"
# >
# wheel: <
#   name: "infra/python/wheels/lxml/${vpython_platform}"
#   version: "version:4.6.3"
# >
# [VPYTHON:END]

import argparse
import os
import pathlib
import site
import sys


EC_BASE = pathlib.Path(__file__).parent.parent

if "ZEPHYR_BASE" in os.environ:
    ZEPHYR_BASE = pathlib.Path(os.environ.get("ZEPHYR_BASE"))
else:
    ZEPHYR_BASE = pathlib.Path(
        EC_BASE.resolve().parent.parent / "third_party" / "zephyr" / "main"
    )

site.addsitedir(ZEPHYR_BASE / "scripts" / "ci")
# pylint:disable=import-error,wrong-import-position
import check_compliance


# pylint:enable=import-error,wrong-import-position


# Fake ref used by "pre-upload.py --pre-submit"
PRE_SUBMIT_REF = "pre-submit"


def _parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "commit",
        help="Git commit to be checked, hash or reference.",
    )

    return parser.parse_args(argv)


def _changed_files_prefix(prefix, commit_range):
    check_compliance.COMMIT_RANGE = commit_range
    check_compliance.GIT_TOP = EC_BASE

    files = check_compliance.get_files(filter="d")
    for file in files:
        if file.startswith(prefix):
            return True

    return False


def main(argv):
    """Main function"""
    args = _parse_args(argv)

    if args.commit == PRE_SUBMIT_REF:
        # Skip if there's no actual commit
        return

    commit_range = f"{args.commit}~1..{args.commit}"

    if not _changed_files_prefix("zephyr/", commit_range):
        # Skip if nothing changed under zephyr/
        return

    # TODO: also enable DevicetreeBindings
    check_compliance.main(
        [
            "--output=",
            "--no-case-output",
            "-m",
            "YAMLLint",
            "-c",
            commit_range,
        ]
    )
    # Never returns, check_compliance.main() calls sys.exit()


if __name__ == "__main__":
    main(sys.argv[1:])
