# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The entry point into zmake."""
import argparse
import inspect
import logging
import pathlib
import sys

import zmake.multiproc as multiproc
import zmake.zmake as zm


def call_with_namespace(func, namespace):
    """Call a function with arguments applied from a Namespace.

    Args:
        func: The callable to call.
        namespace: The namespace to apply to the callable.

    Returns:
        The result of calling the callable.
    """
    kwds = {}
    sig = inspect.signature(func)
    names = [p.name for p in sig.parameters.values()]
    for name, value in vars(namespace).items():
        pyname = name.replace("-", "_")
        if pyname in names:
            kwds[pyname] = value
    return func(**kwds)


# Dictionary used to map log level strings to their corresponding int values.
log_level_map = {
    "DEBUG": logging.DEBUG,
    "INFO": logging.INFO,
    "WARNING": logging.WARNING,
    "ERROR": logging.ERROR,
    "CRITICAL": logging.CRITICAL,
}


def main(argv=None):
    """The main function.

    Args:
        argv: Optionally, the command-line to parse, not including argv[0].

    Returns:
        Zero upon success, or non-zero upon failure.
    """
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--checkout", type=pathlib.Path, help="Path to ChromiumOS checkout"
    )
    parser.add_argument(
        "-D",
        "--debug",
        action="store_true",
        default=False,
        help=("Turn on debug features (e.g., stack trace, " "verbose logging)"),
    )
    parser.add_argument(
        "-j",
        "--jobs",
        # TODO(b/178196029): ninja doesn't know how to talk to a
        # jobserver properly and spams our CPU on all cores.  Default
        # to -j1 to execute sequentially until we switch to GNU Make.
        default=1,
        type=int,
        help="Degree of multiprogramming to use",
    )
    parser.add_argument(
        "-l",
        "--log-level",
        choices=list(log_level_map.keys()),
        dest="log_level",
        help="Set the logging level (default=WARNING)",
    )
    parser.add_argument(
        "-L",
        "--no-log-label",
        action="store_true",
        default=False,
        help="Turn off logging labels",
    )
    parser.add_argument(
        "--modules-dir",
        type=pathlib.Path,
        help="The path to a directory containing all modules "
        "needed.  If unspecified, zmake will assume you have "
        "a Chrome OS checkout and try locating them in the "
        "checkout.",
    )
    parser.add_argument(
        "--zephyr-base", type=pathlib.Path, help="Path to Zephyr OS repository"
    )

    sub = parser.add_subparsers(dest="subcommand", help="Subcommand")
    sub.required = True

    print_versions = sub.add_parser("print-versions")
    print_versions.add_argument(
        "project_dir", type=pathlib.Path, help="Path to the project to build"
    )

    configure = sub.add_parser("configure")
    configure.add_argument(
        "--ignore-unsupported-zephyr-version",
        action="store_true",
        help="Don't warn about using an unsupported Zephyr version",
    )
    configure.add_argument("-t", "--toolchain", help="Name of toolchain to use")
    configure.add_argument(
        "--bringup",
        action="store_true",
        dest="bringup",
        help="Enable bringup debugging features",
    )
    configure.add_argument(
        "-B", "--build-dir", type=pathlib.Path, help="Build directory"
    )
    configure.add_argument(
        "-b",
        "--build",
        action="store_true",
        dest="build_after_configure",
        help="Run the build after configuration",
    )
    configure.add_argument(
        "--test",
        action="store_true",
        dest="test_after_configure",
        help="Test the .elf file after configuration",
    )
    configure.add_argument(
        "project_dir", type=pathlib.Path, help="Path to the project to build"
    )
    configure.add_argument(
        "-c",
        "--coverage",
        action="store_true",
        dest="coverage",
        help="Enable CONFIG_COVERAGE Kconfig.",
    )

    build = sub.add_parser("build")
    build.add_argument(
        "build_dir",
        type=pathlib.Path,
        help="The build directory used during configuration",
    )
    build.add_argument(
        "-w",
        "--fail-on-warnings",
        action="store_true",
        help="Exit with code 2 if warnings are detected",
    )

    test = sub.add_parser("test")
    test.add_argument(
        "build_dir",
        type=pathlib.Path,
        help="The build directory used during configuration",
    )

    sub.add_parser("testall")

    coverage = sub.add_parser("coverage")
    coverage.add_argument(
        "build_dir",
        type=pathlib.Path,
        help="The build directory used during configuration",
    )

    opts = parser.parse_args(argv)

    if opts.no_log_label:
        log_format = "%(message)s"
    else:
        log_format = "%(levelname)s: %(message)s"

    log_level = logging.WARNING
    if opts.log_level:
        log_level = log_level_map[opts.log_level]
    elif opts.debug:
        log_level = logging.DEBUG
    logging.basicConfig(format=log_format, level=log_level)

    if not opts.debug:
        sys.tracebacklimit = 0

    try:
        zmake = call_with_namespace(zm.Zmake, opts)
        subcommand_method = getattr(zmake, opts.subcommand.replace("-", "_"))
        result = call_with_namespace(subcommand_method, opts)
        return result
    finally:
        multiproc.wait_for_log_end()


if __name__ == "__main__":
    sys.exit(main())
