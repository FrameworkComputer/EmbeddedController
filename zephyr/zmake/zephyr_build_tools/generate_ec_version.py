#!/usr/bin/env python3

# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Code to generate the ec_version.h file."""

import argparse
import logging
import os.path
import pathlib
import sys

import zmake.version


def convert_module_list_to_dict(modules: list) -> dict:
    """Convert a list of string paths to modules in to a dict of module
    names to paths."""

    if not modules:
        return {}

    dict_out = {}
    for mod in modules:
        if not mod.is_dir():
            raise FileNotFoundError(f"Module '{mod}' not found")

        dict_out[mod.name] = mod

    return dict_out


def main():
    """CLI entry point for generating the ec_version.h header"""
    logging.basicConfig(level=logging.INFO, stream=sys.stderr)

    parser = argparse.ArgumentParser()

    parser.add_argument("header_path", help="Path to write ec_version.h to")
    parser.add_argument(
        "-s",
        "--static",
        action="store_true",
        help="If set, generate a header which does not include information "
        "like the username, hostname, or date, allowing the build to be"
        "reproducible.",
    )
    parser.add_argument(
        "--base",
        default=os.environ.get("ZEPHYR_BASE"),
        help="Path to Zephyr base directory. Uses ZEPHYR_BASE env var if unset.",
    )
    parser.add_argument(
        "-m",
        "--module",
        action="append",
        help="Specify modules paths to include in version hash. Uses "
        "ZEPHYR_MODULES env var if unset",
    )
    parser.add_argument(
        "-n", "--name", required=True, type=str, help="Project name"
    )

    args = parser.parse_args()

    if args.base is None:
        logging.error(
            "No Zephyr base is defined. Pass --base or set env var ZEPHYR_BASE"
        )
        return 1

    logging.info("Zephyr Base: %s", args.base)

    if args.static:
        logging.info("Using a static version string")

    # Make a dict of modules from the list. Modules can be added one at a time
    # by repeating the -m flag, or once as a semicolon-separated list. In the
    # later case, we need to expand the modules list.

    if args.module is None:
        # No modules specified on command line. Default to environment variable.
        env_modules = os.environ.get("ZEPHYR_MODULES")
        args.module = env_modules.split(";") if env_modules else []
        logging.info(
            "No modules passed via CLI. Getting list from ZEPHYR_MODULES"
        )

    elif len(args.module) == 1:
        # In case of a single -m flag, treat value as a semicolon-delimited
        # list.
        args.module = args.module[0].split(";")

    try:
        module_dict = convert_module_list_to_dict(
            map(pathlib.Path, args.module)
        )
    except FileNotFoundError as err:
        logging.error("Cannot find module: %s", str(err))
        return 1

    logging.info("Including modules: [%s]", ", ".join(args.module))

    # Generate the version string that gets inserted in to the header. Will get
    # commit IDs from Git
    ver = zmake.version.get_version_string(
        args.name, args.base, module_dict, args.static
    )
    logging.info("Version string: %s", ver)

    # Now write the actual header file or put version string in stdout
    if args.header_path == "-":
        print(ver)
    else:
        output_path = pathlib.Path(args.header_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)

        logging.info("Writing header to %s", args.header_path)
        zmake.version.write_version_header(
            ver, output_path, sys.argv[0], args.static
        )

    return 0


def maybe_reexec():
    """Re-exec using the zmake package from the EC source tree, as
    opposed to the system's copy of zmake. This is useful for
    development when engineers need to make changes to zmake. This
    script relies on the zmake package for version string generation
    logic.

    Returns:
        None, if the re-exec did not happen, or never returns if the
        re-exec did happen.
    """
    # We only re-exec if we are inside of a chroot (since if installed
    # standalone using pip, there's already an "editable install"
    # feature for that in pip.)
    env = dict(os.environ)
    srcroot = env.get("CROS_WORKON_SRCROOT")
    if not srcroot:
        return

    # If for some reason we decide to move zmake in the future, then
    # we don't want to use the re-exec logic.
    zmake_path = (
        pathlib.Path(srcroot) / "src" / "platform" / "ec" / "zephyr" / "zmake"
    ).resolve()
    if not zmake_path.is_dir():
        return

    # If PYTHONPATH is set, it is either because we just did a
    # re-exec, or because the user wants to run a specific copy of
    # zmake.  In either case, we don't want to re-exec.
    if "PYTHONPATH" in env:
        return

    # Set PYTHONPATH so that we run zmake from source.
    env["PYTHONPATH"] = str(zmake_path)

    os.execve(sys.argv[0], sys.argv, env)


if __name__ == "__main__":
    maybe_reexec()
    sys.exit(main())
