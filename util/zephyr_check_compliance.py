#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Check a single commit using the Zephyr check_compliance.py script."""

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
        EC_BASE.resolve().parent.parent
        / "third_party"
        / "zephyrproject"
        / "zephyr"
    )

if not os.path.exists(ZEPHYR_BASE):
    raise FileNotFoundError(
        f"ZEPHYR_BASE path does not exist!\nZEPHYR_BASE={ZEPHYR_BASE}"
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


def _patch_get_files():
    """Patch compliance get_files() to exclude non zephyr files."""
    original_get_files = check_compliance.get_files

    def patched_get_files(**kwargs):
        out = []
        for file in original_get_files(**kwargs):
            if file.startswith("zephyr/"):
                out.append(file)
        return out

    check_compliance.get_files = patched_get_files


def _changed_files(commit_range):
    check_compliance.COMMIT_RANGE = commit_range
    check_compliance.GIT_TOP = EC_BASE

    files = check_compliance.get_files(filter="d")
    if len(files) > 0:
        return True

    return False


def main(argv):
    """Main function"""
    args = _parse_args(argv)

    if args.commit == PRE_SUBMIT_REF:
        # Skip if there's no actual commit
        return

    commit_range = f"{args.commit}~1..{args.commit}"

    _patch_get_files()

    if not _changed_files(commit_range):
        # Exit early if nothing changed
        return

    check_compliance.main(
        [
            "--output=",
            "--no-case-output",
            "-m",
            "YAMLLint",
            "-m",
            "DevicetreeBindings",
            "-c",
            commit_range,
        ]
    )
    # Never returns, check_compliance.main() calls sys.exit()


if __name__ == "__main__":
    main(sys.argv[1:])
