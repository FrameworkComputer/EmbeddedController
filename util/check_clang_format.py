#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Validate all C source is formatted with clang-format.

This isn't very useful to users to call directly, but it is run it the
CQ.  Most users will likely find out they forgot to clang-format by
the pre-upload checks.
"""

import logging
import pathlib
import subprocess
import sys

from chromite.lib import commandline


def main(argv=None):
    """Find all C files and runs clang-format on them."""
    parser = commandline.ArgumentParser()
    parser.parse_args(argv)

    logging.info("Validating all code is formatted with clang-format.")
    ec_dir = pathlib.Path(__file__).resolve().parent.parent
    all_files = [
        ec_dir / path
        for path in subprocess.run(
            ["git", "ls-files", "-z"],
            check=True,
            cwd=ec_dir,
            stdout=subprocess.PIPE,
            encoding="utf-8",
        ).stdout.split("\0")
        if path
    ]

    clang_format_files = []
    for path in all_files:
        if not path.is_file() or path.is_symlink():
            continue
        if "third_party" in path.parts:
            continue
        if path.name.endswith(".c") or path.name.endswith(".h"):
            clang_format_files.append(path)

    result = subprocess.run(
        ["clang-format", "--dry-run", *clang_format_files],
        check=False,
        cwd=ec_dir,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )
    if result.stderr:
        logging.error("All C source must be formatted with clang-format!")
        for line in result.stderr.splitlines():
            logging.error("%s", line)
        return 1
    if result.returncode != 0:
        logging.error("clang-format failed with no output!")
        return result.returncode

    logging.info("No clang-format issues found!")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
