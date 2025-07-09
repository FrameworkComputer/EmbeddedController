#!/usr/bin/env vpython3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks files in Zephyr code for references to EC symbols."""

from pathlib import Path
import re
import sys
from typing import Dict, List, Optional

import preupload.lib


EC_FUNCTIONS: Dict[str, str] = {
    "clock": "k_uptime_get_32",
    "crec_msleep": "k_msleep",
    "crec_sleep": "k_sleep(K_SECONDS(sec))",
    "crec_usleep": "k_usleep",
    "force_time": "",
    "time_after": "",
    "time_until": "",
    "udelay": "k_busy_wait",
    "sec_to_date": "gmtime_r",
    "date_to_sec": "timeutil_timegm or timeutil_timegm64",
}

EC_MACROS: Dict[str, str] = {
    "MSEC": "USEC_PER_MSEC",
    "SECOND": "USEC_PER_SEC",
    "SEC_UL": "USEC_PER_SEC",
    "MINUTE": "USEC_PER_SEC * SEC_PER_MIN",
    "HOUR": "USEC_PER_SEC * SEC_PER_HOUR",
}

EC_FUNCTION_REGEXES: Dict[str, re.Pattern] = {
    func: re.compile(rf"\b{func}\b\(") for func in EC_FUNCTIONS
}

EC_MACRO_REGEXES: Dict[str, re.Pattern] = {
    macro: re.compile(rf"\b{macro}\b") for macro in EC_MACROS
}

# Skip checking lines that have // NOLINT_EC_SYMBOL or /* NOLINT_EC_SYMBOL */ comments.
NOLINT_REGEX = re.compile(r"//.*NOLINT_EC_SYMBOL.*|/\*.*NOLINT_EC_SYMBOL.*\*/")

ZEPHYR_PATH = Path("zephyr")


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Look at all the zephyr files in passed in on the commandline for EC symbols."""
    return_code = 0
    args = preupload.lib.parse_args(argv)
    for filename in args.filename:
        try:
            filename.relative_to(ZEPHYR_PATH)
        except ValueError:
            # Not a subdirectory of ZEPHYR_PATH.
            continue
        if not filename.is_file():
            continue
        if not (
            filename.suffix
            in [
                ".c",
                ".h",
                ".cc",
                ".inc",
            ]
        ):
            continue

        lines = preupload.lib.cat_file(args, filename).splitlines()
        for linenum, line in enumerate(lines, start=1):
            if NOLINT_REGEX.search(line):
                continue

            for func, replacement in EC_FUNCTIONS.items():
                if EC_FUNCTION_REGEXES[func].search(line):
                    message = (
                        f"Function '{func}' should not be used in Zephyr code."
                    )
                    if replacement:
                        message += f" Use '{replacement}'."
                    print(
                        f"error: {message}\n" f"{filename}:{linenum}: {line}",
                        file=sys.stderr,
                    )
                    return_code = 1

            for macro, replacement in EC_MACROS.items():
                if EC_MACRO_REGEXES[macro].search(line):
                    message = (
                        f"Macro '{macro}' should not be used in Zephyr code."
                    )
                    if replacement:
                        message += f" Use '{replacement}'."
                    print(
                        f"error: {message}\n" f"{filename}:{linenum}: {line}",
                        file=sys.stderr,
                    )
                    return_code = 1

    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
