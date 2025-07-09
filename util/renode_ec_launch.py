#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A helper utility to launch Renode with the correct configuration."""

from __future__ import annotations

import argparse
import os
import pathlib
import shlex
import subprocess
import sys
from typing import List, Optional


DEFAULT_BOARD = "bloonchipper"
DEFAULT_PROJECT = "ec"

DARTMONKEY_CONSOLE = "sysbus.usart1"

CONSOLE_MAP: dict[str, str] = {
    "bloonchipper": "sysbus.usart2",
    "buccaneer": "sysbus.cr_uart1",
    "dartmonkey": DARTMONKEY_CONSOLE,
    "gwendolin": "sysbus.cr_uart1",
    "helipilot": "sysbus.cr_uart1",
    "nami_fp": DARTMONKEY_CONSOLE,
    "nocturne_fp": DARTMONKEY_CONSOLE,
    "rosalia": "sysbus.cr_uart1",
}

DARTMONKEY_GPIO_WP = "sysbus.gpioPortB.GPIO_WP"
HELIPILOT_GPIO_WP = "sysbus.gpioa.GPIO_WP"

GPIO_WP_MAP: dict[str, str] = {
    "bloonchipper": "sysbus.gpioPortB.GPIO_WP",
    "buccaneer": HELIPILOT_GPIO_WP,
    "dartmonkey": DARTMONKEY_GPIO_WP,
    "helipilot": HELIPILOT_GPIO_WP,
    "gwendolin": HELIPILOT_GPIO_WP,
    "nami_fp": DARTMONKEY_GPIO_WP,
    "nocturne_fp": DARTMONKEY_GPIO_WP,
    "rosalia": HELIPILOT_GPIO_WP,
}

GPIO_WP_ENABLE = "Release"
GPIO_WP_DISABLE = "Press"


def msg_run(cmd: List[str]) -> None:
    """Prints a command and executes it.

    Args:
        cmd: A list of strings representing the command and its arguments.
    """
    print(f"\033[1;32m> {shlex.join(cmd)}\033[m")
    subprocess.run(cmd, check=True)


def launch(
    board: str,
    enable_write_protect: bool,
    zephyr: bool,
    zephyr_bin: str,
    ec_project: str,
) -> int:
    """Launches an EC image in Renode.

    This image can be the actual firmware image or an on-board test image.

    Args:
        board: The name of the EC/Zephyr board.
        enable_write_protect: Whether to enable hardware write protection.
        zephyr: True if running EC-based Zephyr image.
        zephyr_bin: Path to Zephyr binary.
        ec_project: The name of the EC project.
    Returns:
        0 on success, otherwise non-zero.
    """

    # Since we are going to cd later, we need to determine the absolute path
    # of EC.
    script_path = pathlib.Path(__file__).parent.resolve()
    ec_dir = script_path.parent

    if zephyr_bin:
        bin_file = pathlib.Path(zephyr_bin)
        elf_ro_file = pathlib.Path(os.path.dirname(zephyr_bin)) / "zephyr.elf"
        # There is only a single ELF file in upstream Zephyr builds.
        elf_rw_file = elf_ro_file
    elif zephyr:
        out_dir = ec_dir / "build" / "zephyr" / board / "output"
        bin_file = out_dir / "ec.bin"
        elf_ro_file = out_dir / "zephyr.ro.elf"
        elf_rw_file = out_dir / "zephyr.rw.elf"
    else:
        out_dir = ec_dir / "build" / board
        if ec_project != "ec":
            out_dir /= ec_project

        bin_file = out_dir / f"{ec_project}.bin"
        elf_ro_file = out_dir / "RO" / f"{ec_project}.RO.elf"
        elf_rw_file = out_dir / "RW" / f"{ec_project}.RW.elf"

    if not bin_file.exists():
        print(f"Error - The bin file '{bin_file}' does not exist.")
        return 1
    if not elf_ro_file.exists():
        print(f"Error - The elf_ro file '{elf_ro_file}' does not exist.")
        return 1
    if not elf_rw_file.exists():
        print(f"Error - The elf_rw file '{elf_rw_file}' does not exist.")
        return 1

    # Change directory to the EC root for Renode internal relative includes,
    # like "include @util/renode/${board}.resc".
    os.chdir(ec_dir)

    # Outside the chroot, we may not have libicu
    # https://aka.ms/dotnet-missing-libicu
    os.environ["DOTNET_SYSTEM_GLOBALIZATION_INVARIANT"] = "1"

    renode_execute: List[str] = []
    # We set the machine name to the exact board name, since we might be
    # using a derivative board, like buccaneer which is based on helipilot.
    renode_execute.append(f'$name="{board}";')
    renode_execute.append(f'$bin="{bin_file}";')
    renode_execute.append(f'$elf_ro="{elf_ro_file}";')
    renode_execute.append(f'$elf_rw="{elf_rw_file}";')
    renode_execute.append(f"include @util/renode/{board}.resc;")
    # Change logLevel from WARNING to ERROR, since the console is flooded
    # with WARNINGs.
    renode_execute.append("logLevel 3;")
    # https://renode.readthedocs.io/en/latest/debugging/gdb.html
    # (gdb) target remote :3333
    renode_execute.append("machine StartGdbServer 3333;")

    if board in GPIO_WP_MAP:
        wp_state = GPIO_WP_ENABLE if enable_write_protect else GPIO_WP_DISABLE
        renode_execute.append(f"{GPIO_WP_MAP[board]} {wp_state};")

    if board in CONSOLE_MAP:
        # Expose the console UART as a PTY on /tmp/renode-uart. You can connect to
        # the PTY with minicom, screen, etc.
        renode_execute.append(
            'emulation CreateUartPtyTerminal "term" "/tmp/renode-uart" True;'
        )
        renode_execute.append(
            "connector Connect " + CONSOLE_MAP[board] + " term;"
        )

    renode_execute.append("start;")

    # Build the Renode command with script execution.
    renode_cmd: List[str] = ["renode"]
    renode_cmd.append("--console")
    if renode_execute:
        # This is intentionally not shlex.join'ed, since it isn't parsed like
        # shell inside renode.
        renode_execute_str = " ".join(renode_execute)
        renode_cmd += ["--execute", renode_execute_str]

    msg_run(renode_cmd)
    return 0


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """The mainest function."""

    parser = argparse.ArgumentParser(
        description="Launch an EC/Zephyr image in Renode.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "-b",
        "--board",
        choices=CONSOLE_MAP.keys(),
        default=os.environ.get("BOARD", DEFAULT_BOARD),
        help="""
        Name of the EC/Zephyr board.

        The BOARD environment variable can be used instead of this flag.
        """,
    )

    group = parser.add_argument_group(
        "Image Options", "Only one of the following arguments may be used."
    )
    exclusive_group = group.add_mutually_exclusive_group()
    exclusive_group.add_argument(
        "--ec",
        type=str,
        default=os.environ.get("PROJECT", DEFAULT_PROJECT),
        help="""
        Name of the EC project. This is normally just 'ec', but
        could be a test name for on-board test images.

        The PROJECT environment variable can be used instead of this flag.
        """,
    )

    exclusive_group.add_argument(
        "--zephyr", action="store_true", help="Run Zephyr."
    )

    exclusive_group.add_argument(
        "--zephyr-bin",
        type=str,
        help="""
        Full path to a Zephyr binary.

        Used for running upstream Zephyr binaries.
        """,
    )

    parser.add_argument(
        "-w",
        "--enable-write-protect",
        action="store_true",
        help="Enable the hardware write protect GPIO on startup",
    )

    opts = parser.parse_args(argv)
    return launch(
        board=opts.board,
        enable_write_protect=opts.enable_write_protect,
        zephyr=opts.zephyr,
        zephyr_bin=opts.zephyr_bin,
        ec_project=opts.ec,
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
