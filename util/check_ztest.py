#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks files for invalid ZTEST decls."""

import re
import sys
from typing import List, Optional

import preupload.lib


ZTEST_RE = re.compile(
    r"(ZTEST|ZTEST_F|ZTEST_USER|ZTEST_USER_F)\(\w+,\s*($|\w+)"
)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Look at all files passed in on commandline for ZTEST decls where the
    test is not named test_.
    """
    return_code = 0
    parser = preupload.lib.argument_parser()

    args = parser.parse_args(argv)
    for filename in args.filename:
        lines = preupload.lib.cat_file(args, filename).splitlines()
        line_iter = iter(enumerate(lines, start=1))
        for linenum, line in line_iter:
            line = line.rstrip("\n")
            match = ZTEST_RE.search(line)
            if match and match.group(2) == "":
                linenum, line2 = next(line_iter)
                line += line2.rstrip("\n")
                match = ZTEST_RE.search(line)
            if match:
                if not match.group(2).startswith("test_"):
                    print(
                        "error: 'test_' prefix missing from test function name"
                        f"\n{filename}:{linenum}: {match.group(2)}",
                        file=sys.stderr,
                    )
                    return_code = 1
                if "__" in match.group(1):
                    print(
                        "error: suite names should not contain __"
                        f"\n{filename}:{linenum}: {match.group(1)}",
                        file=sys.stderr,
                    )
                    return_code = 1

    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
