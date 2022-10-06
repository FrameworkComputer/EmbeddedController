#!/usr/bin/env python3

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Setup clangd for the given board and image"""

import argparse
import glob
import multiprocessing
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional


def fetch_boards(ec_root: Path) -> Optional[List[str]]:
    """Return a list of boards."""

    base = str(ec_root) + "/board/"

    boards = glob.glob(base + "*")
    if boards is None:
        return None

    return [b[len(base) :] for b in boards]


# We would use image: Literal["RW", "RO"], but it was only added in Python 3.8.
def build(ec_root: Path, board: str, image: str) -> Optional[Path]:
    """Build the correct compile_commands.json."""

    target = Path(f"build/{board}/{image}/compile_commands.json")
    cmd = [
        "make",
        f"-j{multiprocessing.cpu_count()}",
        "BOARD=" + board,
        str(target),
    ]

    print(" ".join(cmd))
    status = subprocess.run(cmd, check=False, cwd=ec_root)

    if status.returncode != 0:
        return None
    return target


def copy(ec_root: Path, target: Path) -> None:
    """Copy the correct compile_commands.json to the EC root."""

    print(f"Copying {target} to EC root.")
    root_cmds_path = ec_root / "compile_commands.json"
    if root_cmds_path.exists():
        root_cmds_path.unlink()
    shutil.copy(ec_root / target, root_cmds_path)


def link(ec_root: Path, target: Path) -> None:
    """Link the correct compile_commands.json to the EC root."""

    print(f"Linking compile_comands.json to {target}")
    root_cmds_path = ec_root / "compile_commands.json"
    if root_cmds_path.exists():
        root_cmds_path.unlink()
    root_cmds_path.symlink_to(target)


def main(argv: List[str]) -> int:
    """Build compile_commands.json for the board/image and lnk to it."""

    parser = argparse.ArgumentParser(description=__doc__)
    # There are far too many boards to show in help message, so we omit the
    # choice option and manually check the boards below.
    parser.add_argument("board", help="EC board name")
    parser.add_argument(
        "image",
        help="EC firmware copy",
        nargs="?",
        choices=["RO", "RW", "ro", "rw"],
        default="RW",
    )
    parser.add_argument(
        "-s", help="symbolically link instead of copying.", action="store_true"
    )
    args = parser.parse_args(argv)

    ec_root = Path(os.path.relpath(os.path.dirname(__file__) + "/.."))

    boards = fetch_boards(ec_root)
    if boards is None:
        parser.error("Can't find boards directory.")
    if not args.board in boards:
        parser.error(f"Board '{args.board}' does not exist.")

    target = build(ec_root, args.board, args.image.upper())
    if target is None:
        print("Failed to build compile_commands.json")
        return 1

    if args.s:
        link(ec_root, target)
    else:
        copy(ec_root, target)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
