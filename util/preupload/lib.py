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
    try:
        if not args.commit or args.commit == PRE_SUBMIT:
            with open(filename, encoding="utf-8") as infile:
                return infile.read()
        return subprocess.run(
            ["git", "show", f"{args.commit}:{filename}"],
            universal_newlines=True,
            stdout=subprocess.PIPE,
            check=True,
        ).stdout
    except ValueError as err:
        raise Exception(f"filename = {filename}") from err


def argument_parser():
    """Returns an ArgumentParser with standard options configured."""
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--commit")
    parser.add_argument("filename", nargs="*", type=Path)
    return parser


def populate_default_filenames(args):
    """Sets the default value of args.filename based on args.commit.

    Follows the same conventions as the cros commands. Specifying a commit but
    no files will run against all files changed in the commit. Specifying a
    commit of "pre-submit" but no files will run against all files in the repo.

    Args:
        args: A dictionary of argument to value from
        argument_parser().parse_args(argv)).

    Returns:
        The args dict, with the "filename" key populated if it was missing.
    """

    if args.filename:
        return args
    if args.commit == PRE_SUBMIT:
        output = subprocess.run(
            ["git", "ls-tree", "-r", "--name-only", "-z", "--", "HEAD"],
            check=True,
            stdout=subprocess.PIPE,
            encoding="utf-8",
        ).stdout
        args.filename = [Path(x) for x in output.split("\0")[:-1]]
    elif args.commit:
        output = subprocess.run(
            [
                "git",
                "diff-tree",
                "--no-commit-id",
                "--name-only",
                "-z",
                "-r",
                args.commit,
            ],
            check=True,
            stdout=subprocess.PIPE,
            encoding="utf-8",
        ).stdout
        args.filename = [Path(x) for x in output.split("\0")[:-1]]
    return args


def parse_args(argv):
    """Parses arguments, and returns the selected options.

    Args:
        argv: List of command line flags passed to main().

    Returns:
        A dict of options parsed from the command line.
    """
    parser = argument_parser()
    args = parser.parse_args(argv)
    return populate_default_filenames(args)
