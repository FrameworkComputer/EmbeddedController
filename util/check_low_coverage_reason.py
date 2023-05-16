#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Ensure commit messages using LOW_COVERAGE_REASON include a bug."""

import logging
import pathlib
import re
import sys

from chromite.lib import commandline
from chromite.lib import cros_build_lib
from chromite.lib import git


# Look for LOW_COVERAGE_REASON and then an optional b{:|/}number bug reference.
LOW_COV_REGEX = re.compile(
    r"\s*(LOW_COVERAGE_REASON=)(?:(?!b[/|:][\d]+).)*(b[/|:]([\d]+))?"
)
EC_BASE = pathlib.Path(__file__).resolve().parent.parent


def main(argv=None):
    """Check for bug in LOW_COVERAGE_REASON."""
    parser = commandline.ArgumentParser()
    parser.add_argument(
        "commit_id",
        help="Commit whose message will be checked.",
    )
    opts = parser.parse_args(argv)

    if opts.commit_id == "pre-submit":
        # Only run check if verifying an actual commit.
        return 0

    try:
        commit_log = git.Log(EC_BASE, rev=opts.commit_id, max_count=1)
    except cros_build_lib.RunCommandError as err:
        logging.error("Unable to query git log: %s", str(err))
        return 1

    # Search commit message for LOW_COVERAGE_REASON and bug
    matches = LOW_COV_REGEX.findall(commit_log)
    if matches and not any({m[2] for m in matches}):
        # We have LOW_COVERAGE_REASON line(s) but none include a bug
        logging.error(
            "LOW_COVERAGE_REASON line must include one or more bugs "
            "tracking the reason for missing coverage"
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
