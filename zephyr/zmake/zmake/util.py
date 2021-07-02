# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Common miscellaneous utility functions for zmake."""

import os
import pathlib
import re
import shlex


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


def locate_zephyr_base(checkout, version):
    """Locate the path to the Zephyr RTOS in a ChromiumOS checkout.

    Args:
        checkout: The path to the ChromiumOS checkout.
        version: The requested zephyr version, as a tuple of integers.

    Returns:
        The path to the Zephyr source.
    """
    return (
        checkout
        / "src"
        / "third_party"
        / "zephyr"
        / "main"
        / "v{}.{}".format(*version[:2])
    )


def read_kconfig_file(path):
    """Parse a Kconfig file.

    Args:
        path: The path to open.

    Returns:
        A dictionary of kconfig items to their values.
    """
    result = {}
    with open(path) as f:
        for line in f:
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
    prog = re.compile(r"^#define\s{}\s(\S+)$".format(key))
    with open(path / "autoconf.h") as f:
        for line in f:
            m = prog.match(line)
            if m:
                return m.group(1)


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
    with open(path, "w") as f:
        for name, value in config.items():
            f.write("{}={}\n".format(name, value))


def parse_zephyr_version(version_string):
    """Parse a human-readable version string (e.g., "v2.4") as a tuple.

    Args:
        version_string: The human-readable version string.

    Returns:
        A 2-tuple or 3-tuple of integers representing the version.
    """
    match = re.fullmatch(r"v?(\d+)[._](\d+)(?:[._](\d+))?", version_string)
    if not match:
        raise ValueError(
            "{} does not look like a Zephyr version.".format(version_string)
        )
    return tuple(int(x) for x in match.groups() if x is not None)


def read_zephyr_version(zephyr_base):
    """Read the Zephyr version from a Zephyr OS checkout.

    Args:
         zephyr_base: path to the Zephyr OS repository.

    Returns:
         A 3-tuple of the version number (major, minor, patchset).
    """
    version_file = pathlib.Path(zephyr_base) / "VERSION"

    file_vars = {}
    with open(version_file) as f:
        for line in f:
            key, sep, value = line.partition("=")
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


def resolve_build_dir(platform_ec_dir, project_dir, build_dir):
    """Resolve the build directory using platform/ec/build/... as default.

    Args:
        platform_ec_dir: The path to the chromiumos source's platform/ec
          directory.
        project_dir: The directory of the project.
        build_dir: The directory to build in (may be None).
    Returns:
        The resolved build directory (using build_dir if not None).
    """
    if build_dir:
        return build_dir

    if not pathlib.Path.exists(project_dir / "zmake.yaml"):
        raise OSError("Invalid configuration")

    # Resolve project_dir to absolute path.
    project_dir = project_dir.resolve()

    # Compute the path of project_dir relative to platform_ec_dir.
    project_relative_path = pathlib.Path.relative_to(project_dir, platform_ec_dir)

    # Make sure that the project_dir is a subdirectory of platform_ec_dir.
    if platform_ec_dir / project_relative_path != project_dir:
        raise OSError(
            "Can't resolve project directory {} which is not a subdirectory"
            " of the platform/ec directory {}".format(project_dir, platform_ec_dir)
        )

    return platform_ec_dir / "build" / project_relative_path
