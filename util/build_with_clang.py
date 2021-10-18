#!/usr/bin/env python3

# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build firmware with clang instead of gcc."""
import argparse
import concurrent
import logging
import multiprocessing
import os
import subprocess
import sys

from concurrent.futures import ThreadPoolExecutor

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
    parser = argparse.ArgumentParser()

    log_level_choices = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
    parser.add_argument(
        '--log_level', '-l',
        choices=log_level_choices,
        default='DEBUG'
    )

    parser.add_argument(
        '--num_threads', '-j',
        type=int,
        default=multiprocessing.cpu_count()
    )

    args = parser.parse_args()
    logging.basicConfig(level=args.log_level)

    logging.debug('Building with %d threads', args.num_threads)

    failed_boards = []
    with ThreadPoolExecutor(max_workers=args.num_threads) as executor:
        future_to_board = {
            executor.submit(build, board): board
            for board in BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG
        }
        for future in concurrent.futures.as_completed(future_to_board):
            board = future_to_board[future]
            try:
                future.result()
            except Exception:
                failed_boards.append(board)

    if len(failed_boards) > 0:
        logging.error('The following boards failed to compile:\n%s',
                      '\n'.join(failed_boards))
        return 1

    logging.info('All boards compiled successfully!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
