#!/usr/bin/env python3
# Copyright 2015 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configuration Option Checker.

Script to ensure that all configuration options for the Chrome EC are defined
in config.h.
"""

import enum
import os
import re
import subprocess
import sys


class Line:
    """Class for each changed line in diff output.

    Attributes:
      line_num: The integer line number that this line appears in the file.
      string: The literal string of this line.
      line_type: '+' or '-' indicating if this line was an addition or
        deletion.
    """

    def __init__(self, line_num, string, line_type):
        """Inits Line with the line number and the actual string."""
        self.line_num = line_num
        self.string = string
        self.line_type = line_type


class Hunk:
    """Class for a git diff hunk.

    Attributes:
      filename: The name of the file that this hunk belongs to.
      lines: A list of Line objects that are a part of this hunk.
    """

    def __init__(self, filename, lines):
        """Inits Hunk with the filename and the list of lines of the hunk."""
        self.filename = filename
        self.lines = lines


# Master file which is supposed to include all CONFIG_xxxx descriptions.
CONFIG_FILE = "include/config.h"

# Specific files which the checker should ignore.
ALLOWLIST = [CONFIG_FILE, "util/config_option_check.py"]

# Specific directories which the checker should ignore.
ALLOW_PATTERN = re.compile("zephyr/.*")

# Specific CONFIG_* flags which the checker should ignore.
ALLOWLIST_CONFIGS = ["CONFIG_ZTEST"]


def obtain_current_config_options():
    """Obtains current config options from include/config.h.

    Scans through the main config file defined in CONFIG_FILE for all CONFIG_*
    options.

    Returns:
      config_options: A list of all the config options in the main CONFIG_FILE.
    """

    config_options = []
    config_option_re = re.compile(r"^#(define|undef)\s+(CONFIG_[A-Z0-9_]+)")
    with open(CONFIG_FILE, "r", encoding="utf-8") as config_file:
        for line in config_file:
            result = config_option_re.search(line)
            if not result:
                continue
            word = result.groups()[1]
            if word not in config_options:
                config_options.append(word)
    return config_options


def obtain_config_options_in_use():
    """Obtains all the config options in use in the repo.

    Scans through the entire repo looking for all CONFIG_* options actively used.

    Returns:
     options_in_use: A set of all the config options in use in the repo.
    """
    file_list = []
    cwd = os.getcwd()
    config_option_re = re.compile(r"\b(CONFIG_[a-zA-Z0-9_]+)")
    config_debug_option_re = re.compile(r"\b(CONFIG_DEBUG_[a-zA-Z0-9_]+)")
    options_in_use = set()
    for dirpath, dirnames, filenames in os.walk(cwd, topdown=True):
        # Ignore the build and private directories (taken from .gitignore)
        for i in range(len(dirnames) - 1, -1, -1):
            if (
                dirnames[i] == "build"
                or dirnames[i] == "private"
                or dirnames[i].startswith("twister-out")
            ):
                del dirnames[i]
        for file in filenames:
            # Ignore hidden files.
            if file.startswith("."):
                continue
            # Only consider C source, assembler, and Make-style files.
            if (
                os.path.splitext(file)[1] in (".c", ".h", ".inc", ".S", ".mk")
                or "Makefile" in file
            ):
                file_list.append(os.path.join(dirpath, file))

    # Search through each file and build a set of the CONFIG_* options being
    # used.

    for file in file_list:
        if CONFIG_FILE in file:
            continue
        with open(file, "r", encoding="utf-8") as cur_file:
            for line in cur_file:
                match = config_option_re.findall(line)
                if match:
                    for option in match:
                        if not in_comment(file, line, option):
                            if option not in options_in_use:
                                options_in_use.add(option)

    # Since debug options can be turned on at any time, assume that they are
    # always in use in case any aren't being used.

    with open(CONFIG_FILE, "r", encoding="utf-8") as config_file:
        for line in config_file:
            match = config_debug_option_re.findall(line)
            if match:
                for option in match:
                    if not in_comment(CONFIG_FILE, line, option):
                        if option not in options_in_use:
                            options_in_use.add(option)

    return options_in_use


def print_missing_config_options(hunks, config_options):
    """Searches thru all the changes in hunks for missing options and prints them.

    Args:
      hunks: A list of Hunk objects which represent the hunks from the git
        diff output.
      config_options: A list of all the config options in the main CONFIG_FILE.

    Returns:
      missing_config_option: A boolean indicating if any CONFIG_* options
        are missing from the main CONFIG_FILE in this commit or if any CONFIG_*
        options removed are no longer being used in the repo.
    """
    missing_config_option = False
    print_banner = True
    deprecated_options = set()
    # Determine longest CONFIG_* length to be used for formatting.
    max_option_length = max(len(option) for option in config_options)
    config_option_re = re.compile(r"\b(CONFIG_[a-zA-Z0-9_]+)")

    # Search for all CONFIG_* options in use in the repo.
    options_in_use = obtain_config_options_in_use()

    # Check each hunk's line for a missing config option.
    for hunk in hunks:
        for line in hunk.lines:
            # Check for the existence of a CONFIG_* in the line.
            match = filter(
                lambda opt: opt in ALLOWLIST_CONFIGS,
                config_option_re.findall(line.string),
            )
            if not match:
                continue

            # At this point, an option was found in the line.  However, we need to
            # verify that it is not within a comment.
            violations = set()

            for option in match:
                if not in_comment(hunk.filename, line.string, option):
                    # Since the CONFIG_* option is not within a comment, we've found a
                    # violation.  We now need to determine if this line is a deletion or
                    # not.  For deletions, we will need to verify if this CONFIG_* option
                    # is no longer being used in the entire repo.

                    if line.line_type == "-":
                        if (
                            option not in options_in_use
                            and option in config_options
                        ):
                            deprecated_options.add(option)
                    else:
                        violations.add(option)

            # Check to see if the CONFIG_* option is in the config file and print the
            # violations.
            for option in match:
                if option not in config_options and option in violations:
                    # Print the banner once.
                    if print_banner:
                        print(
                            "The following config options were found to be missing "
                            f"from {CONFIG_FILE}.\n"
                            "Please add new config options there along with "
                            "descriptions.\n\n"
                        )
                        print_banner = False
                        missing_config_option = True
                    # Print the misssing config option.
                    print(
                        f"> {option:<{max_option_length}} "
                        f"{hunk.filename}:{line.line_num}"
                    )

    if deprecated_options:
        print(
            "\n\nThe following config options are being removed and also appear"
            " to be the last uses\nof that option.  Please remove these "
            f"options from {CONFIG_FILE}.\n\n"
        )
        for option in deprecated_options:
            print(f"> {option}")
            missing_config_option = True

    return missing_config_option


def in_comment(filename, line, substr):
    """Checks if given substring appears in a comment.

    Args:
      filename: The filename where this line is from.  This is used to determine
        what kind of comments to look for.
      line: String of line to search in.
      substr: Substring to search for in the line.

    Returns:
      is_in_comment: Boolean indicating if substr was in a comment.
    """

    c_style_ext = (".c", ".h", ".inc", ".S")
    make_style_ext = ".mk"
    is_in_comment = False

    extension = os.path.splitext(filename)[1]
    substr_idx = line.find(substr)

    # Different files have different comment syntax;  Handle appropriately.
    if extension in c_style_ext:
        beg_comment_idx = line.find("/*")
        end_comment_idx = line.find("*/")
        if end_comment_idx == -1:
            end_comment_idx = len(line)

        if beg_comment_idx == -1:
            # Check to see if this line is from a multi-line comment.
            if line.lstrip().startswith("* "):
                # It _seems_ like it is.
                is_in_comment = True
        else:
            # Check to see if its actually inside the comment.
            if beg_comment_idx < substr_idx < end_comment_idx:
                is_in_comment = True
    elif extension in make_style_ext or "Makefile" in filename:
        beg_comment_idx = line.find("#")
        # Ignore everything to the right of the hash.
        if beg_comment_idx < substr_idx and beg_comment_idx != -1:
            is_in_comment = True
    return is_in_comment


def get_hunks():
    """Gets the hunks of the most recent commit.

    States:
      new_file: Searching for a new file in the git diff.
      filename_search: Searching for the filename of this hunk.
      hunk: Searching for the beginning of a new hunk.
      lines: Counting line numbers and searching for changes.

    Returns:
      hunks: A list of Hunk objects which represent the hunks in the git diff
        output.
    """

    diff = []
    hunks = []
    hunk_lines = []
    line = ""
    filename = ""
    i = 0
    line_num = 0

    # Regex patterns
    new_file_re = re.compile(r"^diff --git")
    filename_re = re.compile(r"^[+]{3} (.*)")
    hunk_line_num_re = re.compile(r"^@@ -[0-9]+,[0-9]+ \+([0-9]+),[0-9]+ @@.*")
    line_re = re.compile(r"^([+| |-])(.*)")

    # Get the diff output.
    proc = subprocess.run(
        [
            "git",
            "diff",
            "--cached",
            "-GCONFIG_*",
            "--no-prefix",
            "--no-ext-diff",
            "HEAD~1",
        ],
        stdout=subprocess.PIPE,
        encoding="utf-8",
        check=True,
    )
    diff = proc.stdout.splitlines()
    if not diff:
        return []
    line = diff[0]

    state = enum.Enum("state", "NEW_FILE FILENAME_SEARCH HUNK LINES")
    current_state = state.NEW_FILE

    while True:
        # Search for the beginning of a new file.
        if current_state is state.NEW_FILE:
            match = new_file_re.search(line)
            if match:
                current_state = state.FILENAME_SEARCH

        # Search the diff output for a file name.
        elif current_state is state.FILENAME_SEARCH:
            # Search for a file name.
            match = filename_re.search(line)
            if match:
                filename = match.groups(1)[0]
                if filename in ALLOWLIST or ALLOW_PATTERN.match(filename):
                    # Skip the file if it's allowlisted.
                    current_state = state.NEW_FILE
                else:
                    current_state = state.HUNK

        # Search for a hunk.  Each hunk starts with a line describing the line
        # numbers in the file.
        elif current_state is state.HUNK:
            hunk_lines = []
            match = hunk_line_num_re.search(line)
            if match:
                # Extract the line number offset.
                line_num = int(match.groups(1)[0])
                current_state = state.LINES

        # Start looking for changes.
        elif current_state is state.LINES:
            # Check if state needs updating.
            new_hunk = hunk_line_num_re.search(line)
            new_file = new_file_re.search(line)
            if new_hunk:
                current_state = state.HUNK
                hunks.append(Hunk(filename, hunk_lines))
                continue
            if new_file:
                current_state = state.NEW_FILE
                hunks.append(Hunk(filename, hunk_lines))
                continue

            match = line_re.search(line)
            if match:
                line_type = match.groups(1)[0]
                # We only care about modifications.
                if line_type != " ":
                    hunk_lines.append(
                        Line(line_num, match.groups(2)[1], line_type)
                    )
                # Deletions don't count towards the line numbers.
                if line_type != "-":
                    line_num += 1

        # Advance to the next line
        try:
            i += 1
            line = diff[i]
        except IndexError:
            # We've reached the end of the diff.  Return what we have.
            if hunk_lines:
                hunks.append(Hunk(filename, hunk_lines))
            return hunks


def main():
    """Searches through committed changes for missing config options.

    Checks through committed changes for CONFIG_* options.  Then checks to make
    sure that all CONFIG_* options used are defined in include/config.h.  Finally,
    reports any missing config options.
    """
    # Obtain the hunks of the commit to search through.
    hunks = get_hunks()
    # Obtain config options from include/config.h.
    config_options = obtain_current_config_options()
    # Find any missing config options from the hunks and print them.
    missing_opts = print_missing_config_options(hunks, config_options)

    if missing_opts:
        print("\nIt may also be possible that you have a typo.")
        sys.exit(1)


if __name__ == "__main__":
    main()
