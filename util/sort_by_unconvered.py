#!/usr/bin/python3
# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Prints out the lines in an lcov info file in order by uncovered lines.

For example to find the file with the most uncovered lines in herobrine (and
therefore the likest easy win) run the coverage commands in zephyr/README.md
and then run:

util/sort_by_unconvered.py build/zephyr/herobrine_final.info | head
"""

import argparse
import re
import subprocess


def main():
    """Main function"""
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument(
        "lcov_files",
        nargs="+",
        metavar="lcov_file",
        help="Name(s) of the lcov files to analyze",
        default=[],
    )
    args = parser.parse_args()

    cmd = ["lcov", "--list-full-path", "--list"] + args.lcov_files
    output = subprocess.run(
        cmd, check=True, stdout=subprocess.PIPE, universal_newlines=True
    ).stdout

    pattern = re.compile(r"^(/\S+)\s*\|\s*([0-9\.]*)%\s*(\d+)\s*\|")
    results = []
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            results.append(
                (
                    match[1],  # Filename
                    match[2],  # Percent
                    int(match[3]),  # Total lines
                    int(
                        float(match[2]) * int(match[3]) / 100.0
                    ),  # Covered Lines
                )
            )

    results.sort(key=lambda x: x[2] - x[3], reverse=True)
    for res in results:
        print(f"{res[0]}: {res[3]}/{res[2]} ({res[1]}%)")


if __name__ == "__main__":
    main()
