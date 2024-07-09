#!/usr/bin/env vpython3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks files for invalid #line directives."""

import re
import sys
from typing import List, Optional

import preupload.lib


LINE_RE = re.compile(r"^\s*#line\s+(\d+)")


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Look at all files passed in on commandline for #line directives."""
    return_code = 0
    args = preupload.lib.parse_args(argv)
    for filename in args.filename:
        if filename.is_file() and filename.suffix in [
            ".c",
            ".h",
            ".cc",
            ".inc",
        ]:
            lines = preupload.lib.cat_file(args, filename).splitlines()
            line_iter = iter(enumerate(lines, start=1))
            for linenum, line in line_iter:
                line = line.rstrip("\n")
                match = LINE_RE.search(line)
                if match and int(match.group(1)) != linenum + 1:
                    print(
                        "error: Invalid #line directive "
                        f"(got {match.group(1)} expected {linenum + 1})"
                        f"\n{filename}:{linenum}: {line}",
                        file=sys.stderr,
                    )
                    return_code = 1
    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
