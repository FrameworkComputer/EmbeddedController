# Lint as: python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common utilities for pre-upload checks."""

import argparse
from pathlib import Path
import subprocess


PRE_SUBMIT = "pre-submit"


def cat_file(args, filename) -> str:
    """Read a file either from disk, or from a git commit."""
    if args.commit == PRE_SUBMIT:
        with open(filename, encoding="utf-8") as infile:
            return infile.read()
    return subprocess.run(
        ["git", "show", f"{args.commit}:{filename}"],
        universal_newlines=True,
        stdout=subprocess.PIPE,
        check=True,
    ).stdout


def argument_parser():
    """Returns an ArgumentParser with standard options configured."""
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--commit", default=PRE_SUBMIT)
    parser.add_argument("filename", nargs="+", type=Path)
    return parser
