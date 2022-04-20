# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The entry point into zmake."""
import argparse
import inspect
import logging
import os
import pathlib
import sys

import zmake.multiproc as multiproc
import zmake.zmake as zm


def maybe_reexec(argv):
    """Re-exec zmake from the EC source tree, if possible and desired.

    Zmake installs into the users' chroot, which makes it convenient
    to execute, but can sometimes become tedious when zmake changes
    land and users haven't upgraded their chroots yet.

    We can partially subvert this problem by re-execing zmake from the
    source if it's available.  This won't make it so developers never
    need to upgrade their chroots (e.g., a toolchain upgrade could
    require chroot upgrades), but at least makes it slightly more
    convenient for an average repo sync.

    Args:
        argv: The argument list passed to the main function, not
            including the executable path.

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

    os.execve(sys.executable, [sys.executable, "-m", "zmake", *argv], env)


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


def get_argparser():
    """Get the argument parser.

    Returns:
        A two tuple, the argument parser, and the subcommand action.
    """
    parser = argparse.ArgumentParser(
        prog="zmake",
        description="Chromium OS's meta-build tool for Zephyr",
    )
    parser.add_argument(
        "--checkout", type=pathlib.Path, help="Path to ChromiumOS checkout"
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
        "--goma",
        action="store_true",
        dest="goma",
        help="Enable hyperspeed compilation with Goma! (Googlers only)",
    )

    log_level_group = parser.add_mutually_exclusive_group()
    log_level_group.add_argument(
        "-l",
        "--log-level",
        choices=log_level_map.values(),
        metavar=f"{{{','.join(log_level_map)}}}",
        type=lambda x: log_level_map[x],
        default=logging.INFO,
        help="Set the logging level (default=INFO)",
    )
    log_level_group.add_argument(
        "-D",
        "--debug",
        dest="log_level",
        action="store_const",
        const=logging.DEBUG,
        help="Alias for --log-level=DEBUG",
    )

    parser.add_argument(
        "-L",
        "--no-log-label",
        action="store_false",
        help="Turn off logging labels",
        dest="log_label",
        default=None,
    )
    parser.add_argument(
        "--log-label",
        action="store_true",
        help="Turn on logging labels",
        dest="log_label",
        default=None,
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

    sub = parser.add_subparsers(
        dest="subcommand",
        metavar="subcommand",
        help="Subcommand to run",
    )
    sub.required = True

    configure = sub.add_parser(
        "configure",
        help="Set up a build directory to be built later by the build subcommand",
    )
    add_common_configure_args(configure)

    build = sub.add_parser(
        "build",
        help="Configure and build projects",
    )
    add_common_configure_args(build)

    list_projects = sub.add_parser(
        "list-projects",
        help="List projects known to zmake.",
    )
    list_projects.add_argument(
        "--format",
        default="{config.project_name}\n",
        dest="fmt",
        help=(
            "Output format to print projects (str.format(config=project.config) is "
            "called on this for each project)."
        ),
    )
    list_projects.add_argument(
        "search_dir",
        type=pathlib.Path,
        nargs="?",
        help="Optional directory to search for BUILD.py files in.",
    )

    test = sub.add_parser(
        "test",
        help="Configure, build and run tests on specified projects",
    )
    test.add_argument(
        "--no-rebuild",
        action="store_true",
        help="Do not configure or build before running tests.",
    )
    add_common_configure_args(test)

    testall = sub.add_parser(
        "testall",
        help="Alias for test --all",
    )
    testall.add_argument(
        "--clobber",
        action="store_true",
        dest="clobber",
        help="Delete existing build directories, even if configuration is unchanged",
    )
    testall.add_argument("-B", "--build-dir", type=pathlib.Path, help="Build directory")

    generate_readme = sub.add_parser(
        "generate-readme",
        help="Update the auto-generated markdown documentation",
    )
    generate_readme.add_argument(
        "-o",
        "--output-file",
        default=pathlib.Path(__file__).parent.parent / "README.md",
        help="File to write to.  It will only be written if changed.",
    )
    generate_readme.add_argument(
        "--diff",
        action="store_true",
        help=(
            "If specified, diff the README with the expected contents instead of "
            "writing out."
        ),
    )

    return parser, sub


def add_common_configure_args(sub_parser: argparse.ArgumentParser):
    """Adds common arguments used by configure-like subcommands."""
    sub_parser.add_argument("-t", "--toolchain", help="Name of toolchain to use")
    sub_parser.add_argument(
        "--bringup",
        action="store_true",
        dest="bringup",
        help="Enable bringup debugging features",
    )
    sub_parser.add_argument(
        "--clobber",
        action="store_true",
        dest="clobber",
        help="Delete existing build directories, even if configuration is unchanged",
    )
    sub_parser.add_argument(
        "--allow-warnings",
        action="store_true",
        default=False,
        help="Do not treat warnings as errors",
    )
    sub_parser.add_argument(
        "-B",
        "--build-dir",
        type=pathlib.Path,
        help="Root build directory, project files will be in "
        "${build_dir}/${project_name}",
    )
    sub_parser.add_argument(
        "-c",
        "--coverage",
        action="store_true",
        dest="coverage",
        help="Enable CONFIG_COVERAGE Kconfig.",
    )
    sub_parser.add_argument(
        "--extra-cflags",
        help="Additional CFLAGS to use for target builds",
    )
    group = sub_parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "-a",
        "--all",
        action="store_true",
        dest="all_projects",
        help="Select all projects",
    )
    group.add_argument(
        "--host-tests-only",
        action="store_true",
        dest="host_tests_only",
        help="Select all test projects",
    )
    group.add_argument(
        "project_names",
        nargs="*",
        metavar="project_name",
        help="Name(s) of the project(s) to build",
        default=[],
    )


def main(argv=None):
    """The main function.

    Args:
        argv: Optionally, the command-line to parse, not including argv[0].

    Returns:
        Zero upon success, or non-zero upon failure.
    """
    if argv is None:
        argv = sys.argv[1:]

    maybe_reexec(argv)

    parser, _ = get_argparser()
    opts = parser.parse_args(argv)

    # Default logging
    log_label = False

    if opts.log_label is not None:
        log_label = opts.log_label
    if log_label:
        log_format = "%(levelname)s: %(message)s"
    else:
        log_format = "%(message)s"
        multiproc.LOG_JOB_NAMES = False

    logging.basicConfig(format=log_format, level=opts.log_level)

    zmake = call_with_namespace(zm.Zmake, opts)
    try:
        subcommand_method = getattr(zmake, opts.subcommand.replace("-", "_"))
        result = call_with_namespace(subcommand_method, opts)
        wait_rv = zmake.executor.wait()
        return result or wait_rv
    finally:
        multiproc.LogWriter.wait_for_log_end()
        if zmake.failed_projects:
            logging.error("Failed projects: %s", zmake.failed_projects)


if __name__ == "__main__":
    sys.exit(main())
