#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Updated generated Bazel files for EC targets."""

import argparse
from concurrent import futures
from pathlib import Path
import site
import subprocess
import sys
from typing import List, Optional


EC_DIR = Path(__file__).resolve().parent.parent
LEGACY_EC_BOARDS_DIR = EC_DIR / "board"
ZEPHYR_DIR = EC_DIR / "zephyr"
ZMAKE_DIR = ZEPHYR_DIR / "zmake"
DEFAULT_OUTPUT = EC_DIR / "bazel" / "all_targets.generated.bzl"


def find_checkout():
    """Find the path to the base of the checkout (e.g., ~/chromiumos)."""
    for path in EC_DIR.parents:
        if (path / ".repo").is_dir():
            return path
    raise FileNotFoundError("Unable to locate the root of the checkout")


site.addsitedir(find_checkout())
site.addsitedir(ZMAKE_DIR)


import zmake.project  # pylint: disable=import-error,wrong-import-position

from chromite.format import formatters  # pylint: disable=wrong-import-position


def _find_zephyr_ec_projects():
    """Find all Zephyr EC projects."""
    modules = zmake.modules.locate_from_checkout(find_checkout())
    projects_path = zmake.modules.default_projects_dirs(modules)

    for project in zmake.project.find_projects(projects_path).values():
        result = {"board": project.config.project_name}
        extra_modules = [
            x
            for x in project.config.modules + project.config.optional_modules
            if x != "ec"
        ]
        if extra_modules:
            result["extra_modules"] = extra_modules
        yield project.config.project_name, result


def _get_legacy_ec_make_vars(board):
    """Get the make variables for a Legacy EC board."""
    make_cmd = subprocess.run(
        ["make", f"BOARD={board}", "SHELL=/bin/bash", "print-make-vars"],
        check=True,
        encoding="utf-8",
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    make_vars = {}
    for line in make_cmd.stdout.splitlines():
        var, _, val = line.partition("=")
        make_vars[var] = val
    return make_vars


def _find_legacy_ec_projects():
    """Find all Legacy EC projects."""
    # Parallelize, shell is slow.
    board_dirs = list(LEGACY_EC_BOARDS_DIR.iterdir())
    make_vars_futures = []

    with futures.ThreadPoolExecutor() as executor:
        for board_dir in board_dirs:
            make_vars_futures.append(
                executor.submit(_get_legacy_ec_make_vars, board_dir.name)
            )

        for board_dir, future in zip(board_dirs, make_vars_futures):
            board = board_dir.name
            result = {"board": board, "zephyr": False}
            if board_dir.is_symlink():
                result["real_board"] = board_dir.resolve().name
            make_vars = future.result()
            result["chip"] = make_vars["CHIP"]
            result["core"] = make_vars["CORE"]
            baseboard = make_vars.get("BASEBOARD")
            if baseboard:
                result["baseboard"] = baseboard
            yield board, result


def _find_all_projects():
    """Find all projects, Zephyr and Legacy."""
    zephyr_projects = dict(_find_zephyr_ec_projects())
    legacy_projects = dict(_find_legacy_ec_projects())

    # Rename duplicate projects in zephyr as ${project}_zephyr.
    all_projects = dict(legacy_projects)
    for name, args in zephyr_projects.items():
        if name in all_projects:
            all_projects[f"{name}_zephyr"] = args
        else:
            all_projects[name] = args

    return all_projects


def _gen_bazel_lines():
    """Generate the lines of the Bazel file."""
    # First, the copyright header.  Steal it from this file.
    yield from Path(__file__).read_text(encoding="utf-8").splitlines()[1:4]
    yield ""

    yield "# This file is auto-generated.  To update, run:"
    yield "# ./bazel/gen_bazel_targets.py"
    yield ""

    yield 'load(":ec_target.bzl", "ec_target")'
    yield ""

    yield "def all_targets():"
    for name, args in sorted(_find_all_projects().items()):
        yield "    ec_target("
        yield f"        name = {name!r},"
        for key, value in sorted(args.items()):
            yield f"        {key} = {value!r},"
        yield "    )"


def gen_bazel():
    """Generate the source code that should be in the Bazel file."""
    unformatted_code = "".join(f"{x}\n" for x in _gen_bazel_lines())
    return formatters.star.Data(unformatted_code, DEFAULT_OUTPUT)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """The main function."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-o",
        "--output-file",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="The targets Bazel output path.",
    )
    opts = parser.parse_args(argv)

    opts.output_file.write_text(gen_bazel())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
