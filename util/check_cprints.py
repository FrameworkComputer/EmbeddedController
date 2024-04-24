#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks files for invalid CPRINTS calls."""

import re
import sys
from typing import List, Optional

import preupload.lib


CPRINTS_RE = re.compile(r'(CPRINTS|cprints)[^"]*"[^"]*\\n"')


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Look at all files passed in on commandline for invalid CPRINTS calls."""
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
            for linenum, line in enumerate(lines, start=1):
                if CPRINTS_RE.search(line):
                    print(
                        "error: CPRINTS strings should not include newline "
                        f"characters\n{filename}:{linenum}: {line}",
                        file=sys.stderr,
                    )
                    return_code = 1

    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
