#!/usr/bin/env vpython3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks ec_commands.h for duplicate ordinals."""

import re
import sys
from typing import List, Optional

import preupload.lib


EC_CMD_RE = re.compile(r"^#define\s+(EC_CMD_[^\s(]+)\s+(\S+)")


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Check all EC_CMD ordinals for duplicates."""
    return_code = 0
    args = preupload.lib.parse_args(argv)

    ec_commands = {}
    for filename in args.filename:
        if filename.name == "ec_commands.h":
            lines = preupload.lib.cat_file(args, filename).splitlines()
            for linenum, line in enumerate(lines, start=1):
                mmm = EC_CMD_RE.match(line)
                if not mmm:
                    continue
                ordinal = int(mmm.group(2), 0)
                if ordinal in ec_commands:
                    if (
                        mmm.group(1) == "EC_CMD_PORT80_READ"
                        and ec_commands[ordinal] == "EC_CMD_PORT80_LAST_BOOT"
                    ):
                        continue
                    sys.stderr.write(
                        f"{filename}:{linenum} - Duplicate host command: "
                        f"{mmm.group(1)} set to {ordinal}, conflicts with "
                        f"{ec_commands[ordinal]}\n"
                    )
                    return_code = 1
                ec_commands[ordinal] = mmm.group(1)
    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
