#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Setup clangd for the given board and image"""

import argparse
import glob
import multiprocessing
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import List, Optional


def ec_fetch_boards(ec_root: Path) -> Optional[List[str]]:
    """Return a list of EC boards seen."""

    base = str(ec_root) + "/board/"

    boards = glob.glob(base + "*")
    if boards is None:
        return None

    return [b[len(base) :] for b in boards]


def zephyr_fetch_projects(ec_root: Path) -> Optional[List[str]]:
    """Return a list of Zephyr projects seen."""

    base = str(ec_root) + "/zephyr/program/"

    boards = glob.glob(base + "*")
    if boards is None:
        return None

    return [b[len(base) :] for b in boards]


# We would use image: Literal["RW", "RO"], but it was only added in Python 3.8.
def ec_build(ec_root: Path, board: str, image: str) -> Optional[Path]:
    """Build the correct compile_commands.json for EC board/image."""

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


# We would use image: Literal["RW", "RO"], but it was only added in Python 3.8.
def zephyr_build(ec_root: Path, board: str, image: str) -> Optional[Path]:
    """Build the correct compile_commands.json for Zephyr board/image"""

    target = Path(
        f"build/zephyr/{board}/build-{image.lower()}/compile_commands.json"
    )
    cmd = ["zmake", "configure", board]

    print(" ".join(cmd))
    status = subprocess.run(cmd, check=False, cwd=ec_root)

    if status.returncode != 0:
        return None

    # Replace /mnt/host/source with path of chromiumos outside chroot
    default_chromiumos_path_outside_chroot = os.path.join(
        Path.home(), "chromiumos"
    )
    chromiumos_path_outside_chroot = os.environ.get(
        "EXTERNAL_TRUNK_PATH", default_chromiumos_path_outside_chroot
    )
    chromiumos_path_inside_chroot = "/mnt/host/source"

    print(
        f"Replacing '{chromiumos_path_inside_chroot}' with "
        + f"'{chromiumos_path_outside_chroot}' in file {target}"
    )

    target.write_text(
        target.read_text().replace(
            chromiumos_path_inside_chroot, chromiumos_path_outside_chroot
        )
    )

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
        choices=["ro", "rw"],
        default="rw",
        type=str.lower,
    )
    parser.add_argument(
        "-s", help="symbolically link instead of copying.", action="store_true"
    )
    parser.add_argument(
        "--os",
        help="OS used to build board",
        nargs="?",
        choices=["auto", "ec", "zephyr"],
        default="auto",
        type=str.lower,
    )
    args = parser.parse_args(argv)

    ec_root = Path(os.path.relpath(os.path.dirname(__file__) + "/.."))

    ec_boards = ec_fetch_boards(ec_root)
    if ec_boards is None:
        parser.error("Can't find EC boards directory.")

    zephyr_projects = zephyr_fetch_projects(ec_root)
    if zephyr_projects is None:
        parser.error("Can't find Zephyr projects directory.")

    is_in_ec = args.board in ec_boards
    is_in_zephyr = args.board in zephyr_projects

    if not is_in_ec and not is_in_zephyr:
        parser.error(f"Board '{args.board}' does not exist.")

    # When "os" is auto, try Zephyr first, fall back to EC.
    os_selection = args.os
    if os_selection == "auto":
        if is_in_zephyr:
            os_selection = "zephyr"
        else:
            os_selection = "ec"

    print(f"Configuring for {os_selection}.")

    if os_selection == "ec":
        target = ec_build(ec_root, args.board, args.image.upper())
    else:
        target = zephyr_build(ec_root, args.board, args.image.upper())

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
