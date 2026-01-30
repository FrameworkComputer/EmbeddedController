#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Find broken symlinks."""

import sys
from typing import List, Optional

import preupload.lib


def main(argv: Optional[List[str]] = None) -> int:
    """Check the passed in files for broken symlinks."""
    return_code = 0
    args = preupload.lib.parse_args(argv)
    for filename in args.filename:
        if filename.is_symlink() and not filename.exists():
            print(
                f"error: broken symlink {filename} -> {filename.readlink()}",
                file=sys.stderr,
            )
            return_code = 1
    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
