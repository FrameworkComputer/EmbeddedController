# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common miscellaneous utility functions for zmake."""

import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys

import zmake.jobserver
import zmake.multiproc


def c_str(input_str):
    """Make a string that can be included as a literal in C source code.

    Args:
        input_str: The string to process.

    Returns:
        A string which can be included in C source code.
    """

    def c_chr(char):
        # Convert a char in a string to the C representation.  Per the
        # C standard, we can use all characters but quote, newline,
        # and backslash directly with no replacements.
        return {
            '"': r"\"",
            "\n": r"\n",
            "\\": "\\\\",
        }.get(char, char)

    return f'"{"".join(map(c_chr, input_str))}"'


def locate_cros_checkout():
    """Find the path to the ChromiumOS checkout.

    Returns:
        The first directory found with a .repo directory in it,
        starting by checking the CROS_WORKON_SRCROOT environment
        variable, then scanning upwards from the current directory,
        and finally from a known set of common paths.
    """

    def propose_checkouts():
        yield os.getenv("CROS_WORKON_SRCROOT")

        path = pathlib.Path.cwd()
        while path.resolve() != pathlib.Path("/"):
            yield path
            path = path / ".."

        yield "/mnt/host/source"
        yield pathlib.Path.home() / "trunk"
        yield pathlib.Path.home() / "chromiumos"

    for path in propose_checkouts():
        if not path:
            continue
        path = pathlib.Path(path)
        if (path / ".repo").is_dir():
            return path.resolve()

    raise FileNotFoundError("Unable to locate a ChromiumOS checkout")


def read_kconfig_file(path):
    """Parse a Kconfig file.

    Args:
        path: The path to open.

    Returns:
        A dictionary of kconfig items to their values.
    """
    result = {}
    with open(path, encoding="utf-8") as file:
        for line in file:
            line, _, _ = line.partition("#")
            line = line.strip()
            if line:
                name, _, value = line.partition("=")
                result[name.strip()] = value.strip()
    return result


def read_kconfig_autoconf_value(path, key):
    """Parse an autoconf.h file for a resolved kconfig value

    Args:
        path: The path to the autoconf.h file.
        key: The define key to lookup.

    Returns:
        The value associated with the key or nothing if the key wasn't found.
    """
    prog = re.compile(rf"^#define\s{key}\s(\S+)$")
    with open(path / "autoconf.h", encoding="utf-8") as file:
        for line in file:
            match = prog.match(line)
            if match:
                return match.group(1)
    return None


def write_kconfig_file(path, config, only_if_changed=True):
    """Write out a dictionary to Kconfig format.

    Args:
        path: The path to write to.
        config: The dictionary to write.
        only_if_changed: Set to True if the file should not be written
            unless it has changed.
    """
    if only_if_changed:
        if path.exists() and read_kconfig_file(path) == config:
            return
    with open(path, "w", encoding="utf-8") as file:
        for name, value in config.items():
            file.write(f"{name}={value}\n")


def read_zephyr_version(zephyr_base):
    """Read the Zephyr version from a Zephyr OS checkout.

    Args:
         zephyr_base: path to the Zephyr OS repository.

    Returns:
         A 3-tuple of the version number (major, minor, patchset).
    """
    version_file = pathlib.Path(zephyr_base) / "VERSION"

    file_vars = {}
    with open(version_file, encoding="utf-8") as file:
        for line in file:
            key, _, value = line.partition("=")
            file_vars[key.strip()] = value.strip()

    return (
        int(file_vars["VERSION_MAJOR"]),
        int(file_vars["VERSION_MINOR"]),
        int(file_vars["PATCHLEVEL"]),
    )


def repr_command(argv):
    """Represent an argument array as a string.

    Args:
        argv: The arguments of the command.

    Returns:
        A string which could be pasted into a shell for execution.
    """
    return " ".join(shlex.quote(str(arg)) for arg in argv)


def update_symlink(target_path, link_path):
    """Create a symlink if it does not exist, or links to a different path.

    Args:
        target_path: A Path-like object of the desired symlink path.
        link_path: A Path-like object of the symlink.
    """
    target = target_path.resolve()
    if (
        not link_path.is_symlink()
        or pathlib.Path(os.readlink(link_path)).resolve() != target
    ):
        if link_path.exists():
            link_path.unlink()
        link_path.symlink_to(target)


def log_multi_line(logger, level, message):
    """Log a potentially multi-line message to the logger.

    Args:
        logger: The Logger object to log to.
        level: The logging level to use when logging.
        message: The (potentially) multi-line message to log.
    """
    for line in message.splitlines():
        if line:
            logger.log(level, line)


def get_tool_path(program):
    """Get the path to a program.

    First, the TOOL_PATH_${program} environment variable is checked,
    and if unspecified, the PATH environment variable is searched.

    This allows callers to customize which tools zmake uses.

    Args:
        program: The program to locate

    Returns:
        The Path to the program.
    """
    path = os.environ.get(f"TOOL_PATH_{program}")
    if path:
        path = pathlib.Path(path).resolve()
    else:
        path = pathlib.Path(shutil.which(program))
    if not path:
        raise FileNotFoundError(f"{program} not found in PATH")
    return path


def get_lcov_options(tool_path):
    """Return required options for running lcov.

    Varying versions of lcov require different options to successfully work.
    This function executes lcov to discover its version and returns arguments
    appropriate for the detected version.

    Args:
        tool_path: The path to a lcov binary (as from get_tool_path("lcov"))

    Returns:
        List of strings to be passed to lcov in argv
    """
    version_result = subprocess.run(
        [tool_path, "--version"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=True,
        text=True,
    )
    version_match = re.search(
        r"LCOV version (?P<major>\d+)\.(?P<minor>\d+)(?P<extra>.*)$",
        version_result.stdout,
    )
    if not version_match:
        raise ValueError(
            f"Unable to determine version of lcov: output was: {version_result.stdout:?}"
        )

    if int(version_match["major"]) < 2:
        return ["--rc", "lcov_branch_coverage=1"]
    # 2.0 deprecated the lcov_ prefix from rc options and began treating
    # many previously-ignored errors as fatal, some of which need to be
    # ignored.
    return [
        "--rc",
        "branch_coverage=1",
        "--filter",
        "range",
        "--ignore-errors",
        "inconsistent,source",
        "--ignore-errors",
        "gcov,gcov",
    ]


def merge_token_databases(databases, merged_db):
    """Merge token databases

    Args:
    databases: Array of token databases to be merged
    merged_db: Merged token database output path.
    """
    checkout = locate_cros_checkout()
    modules = zmake.modules.locate_from_checkout(pathlib.Path("."))
    jobclient = zmake.jobserver.GNUMakeJobServer()

    proc = jobclient.popen(
        [
            sys.executable,
            checkout
            / modules["pigweed"]
            / "pw_tokenizer"
            / "py"
            / "pw_tokenizer"
            / "database.py",
            "create",
            "--type",
            "binary",
            "--force",
            "--database",
            merged_db,
            *databases,
        ],
        cwd=os.path.dirname(merged_db),
        encoding="utf-8",
    )

    if proc.wait(timeout=60):
        raise OSError("Failed to run PW database.py")
