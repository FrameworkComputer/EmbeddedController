#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool to manage the fingerprint system on ChromeOS."""

import argparse
import os
import shutil
import subprocess
import sys


def cmd_flash(args: argparse.Namespace) -> int:
    """Flash the entire firmware FPMCU using the native bootloader.

    This requires the Chromebook to be in dev mode with hardware write protect
    disabled.
    """

    if not shutil.which("flash_fp_mcu"):
        print("Error - The flash_fp_mcu utility does not exist.")
        return 1

    cmd = ["flash_fp_mcu"]
    if args.image:
        if not os.path.isfile(args.image):
            print(f"Error - image {args.image} is not a file.")
            return 1
        cmd.append(args.image)

    print(f'Running {" ".join(cmd)}.')
    sys.stdout.flush()
    p = subprocess.run(cmd)  # pylint: disable=subprocess-run-check
    return p.returncode


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="subcommand", title="subcommands")
    # This method of setting required is more compatible with older python.
    subparsers.required = True

    # Parser for "flash" subcommand.
    parser_flash = subparsers.add_parser("flash", help=cmd_flash.__doc__)
    parser_flash.add_argument(
        "image", nargs="?", help="Path to the firmware image"
    )
    parser_flash.set_defaults(func=cmd_flash)
    opts = parser.parse_args(argv)
    return opts.func(opts)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
