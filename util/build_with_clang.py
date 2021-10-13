#!/usr/bin/env python3

# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build firmware with clang instead of gcc."""
import logging
import os
import subprocess
import sys


# Add to this list as compilation errors are fixed for boards.
BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG = [
    'dartmonkey',
    'bloonchipper',
    'nucleo-f412zg',
    'nucleo-h743zi',
]


def build(board_name: str) -> None:
    """Build with clang for specified board."""
    logging.debug('Building board: "%s"', board_name)

    cmd = [
        'make',
        'BOARD=' + board_name,
        '-j',
    ]

    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd, env=dict(os.environ, CC='clang'), check=True)


def main() -> int:
    logging.basicConfig(level='DEBUG')
    for board in BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG:
        build(board)

    return 0


if __name__ == '__main__':
    sys.exit(main())
