#!/usr/bin/env vpython3

# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This script is a wrapper for invoking Twister, the Zephyr test runner, using
default parameters for the ChromiumOS EC project. For an overview of CLI
parameters that may be used, please consult the Twister documentation.
"""

# [VPYTHON:BEGIN]
# python_version: "3.8"
# wheel: <
#   name: "infra/python/wheels/anytree-py2_py3"
#   version: "version:2.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/colorama-py3"
#   version: "version:0.4.1"
# >
# wheel: <
#   name: "infra/python/wheels/docopt-py2_py3"
#   version: "version:0.6.2"
# >
# wheel: <
#   name: "infra/python/wheels/ply-py2_py3"
#   version: "version:3.11"
# >
# wheel: <
#   name: "infra/python/wheels/psutil/${vpython_platform}"
#   version: "version:5.8.0.chromium.3"
# >
# wheel: <
#   name: "infra/python/wheels/pykwalify-py2_py3"
#   version: "version:1.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/pyserial-py2_py3"
#   version: "version:3.4"
# >
# wheel: <
#   name: "infra/python/wheels/python-dateutil-py2_py3"
#   version: "version:2.8.1"
# >
# wheel: <
#   name: "infra/python/wheels/pyyaml-py3"
#   version: "version:5.3.1"
# >
# wheel: <
#   name: "infra/python/wheels/ruamel_yaml_clib/${vpython_platform}"
#   version: "version:0.2.6"
# >
# wheel: <
#   name: "infra/python/wheels/ruamel_yaml-py3"
#   version: "version:0.17.16"
# >
# wheel: <
#   name: "infra/python/wheels/six-py2_py3"
#   version: "version:1.16.0"
# >
# [VPYTHON:END]

import argparse
import os
import shlex
import subprocess
import sys
from pathlib import Path


def find_checkout() -> Path:
    """Find the location of the source checkout or return None."""
    cros_checkout = os.environ.get("CROS_WORKON_SRCROOT")
    if cros_checkout is not None:
        return Path(cros_checkout)

    # Attempt to locate checkout location relatively if being run outside of chroot.
    try:
        cros_checkout = Path(__file__).resolve().parents[4]
        assert (cros_checkout / "src").exists()
        return cros_checkout
    except (IndexError, AssertionError):
        # Not in the chroot or matching directory structure
        return None


def find_paths():
    """Find EC base, Zephyr base, and Zephyr modules paths and return as a 3-tuple."""

    # Determine where the source tree is checked out. Will be None if operating outside
    # of the chroot (e.g. Gitlab builds). In this case, additional paths need to be
    # passed in through environment variables.
    cros_checkout = find_checkout()

    if cros_checkout:
        ec_base = cros_checkout / "src" / "platform" / "ec"
        zephyr_base = cros_checkout / "src" / "third_party" / "zephyr" / "main"
        zephyr_modules_dir = cros_checkout / "src" / "third_party" / "zephyr"
    else:
        try:
            ec_base = Path(os.environ["EC_DIR"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "EC_DIR unspecified. Please pass as env var or use chroot."
            ) from err

        try:
            zephyr_base = Path(os.environ["ZEPHYR_BASE"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "ZEPHYR_BASE unspecified. Please pass as env var or use chroot."
            ) from err

        try:
            zephyr_modules_dir = Path(os.environ["MODULES_DIR"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "MODULES_DIR unspecified. Please pass as env var or use chroot."
            ) from err

    return (ec_base, zephyr_base, zephyr_modules_dir)


def find_modules(mod_dir: Path) -> list:
    """Find Zephyr modules in the given directory `dir`."""

    modules = []
    for child in mod_dir.iterdir():
        if child.is_dir() and (child / "zephyr" / "module.yml").exists():
            modules.append(child.resolve())
    return modules


def main():
    """Run Twister using defaults for the EC project."""

    # Get paths for the build.
    ec_base, zephyr_base, zephyr_modules_dir = find_paths()

    zephyr_modules = find_modules(zephyr_modules_dir)

    # Add the EC dir as a module if not already included (resolve all paths to
    # account for symlinked or relative paths)
    if ec_base.resolve() not in zephyr_modules:
        zephyr_modules.append(ec_base)

    # Prepare environment variables for export to Twister. Inherit the parent
    # process's environment, but set some default values if not already set.
    twister_env = dict(os.environ)
    is_in_chroot = Path("/etc/cros_chroot_version").is_file()
    extra_env_vars = {
        "TOOLCHAIN_ROOT": os.environ.get(
            "TOOLCHAIN_ROOT",
            str(ec_base / "zephyr") if is_in_chroot else zephyr_base,
        ),
        "ZEPHYR_TOOLCHAIN_VARIANT": os.environ.get(
            "ZEPHYR_TOOLCHAIN_VARIANT", "llvm" if is_in_chroot else "host"
        ),
    }
    twister_env.update(extra_env_vars)

    # Twister CLI args
    # TODO(b/239165779): Reduce or remove the usage of label properties
    # Zephyr upstream has deprecated the label property. We need to allow
    # warnings during twister runs until all the label properties are removed
    # from all board and test overlays.
    twister_cli = [
        sys.executable,
        str(zephyr_base / "scripts" / "twister"),  # Executable path
        "--ninja",
        "--disable-warnings-as-errors",
        f"-x=DTS_ROOT={str( ec_base / 'zephyr')}",
        f"-x=SYSCALL_INCLUDE_DIRS={str(ec_base / 'zephyr' / 'include' / 'drivers')}",
        f"-x=ZEPHYR_BASE={zephyr_base}",
        f"-x=ZEPHYR_MODULES={';'.join([str(p) for p in zephyr_modules])}",
    ]

    # `-T` flags (used for specifying test directories to build and run)
    # require special handling. When run without `-T` flags, Twister will
    # search for tests in `zephyr_base`. This is undesirable and we want
    # Twister to look in the EC tree by default, instead. Use argparse to
    # intercept `-T` flags and pass in a new default if none are found. If
    # user does pass their own `-T` flags, pass them through instead. Do the
    # same with verbosity. Other arguments get passed straight through,
    # including -h/--help so that Twister's own help text gets displayed.
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument("-T", "--testsuite-root", action="append")
    parser.add_argument("-p", "--platform", action="append")
    parser.add_argument("-v", "--verbose", action="count", default=0)
    parser.add_argument(
        "--gcov-tool", default=str(ec_base / "util" / "llvm-gcov.sh")
    )
    intercepted_args, other_args = parser.parse_known_args()

    for _ in range(intercepted_args.verbose):
        # Pass verbosity setting through to twister
        twister_cli.append("-v")

    if intercepted_args.testsuite_root:
        # Pass user-provided -T args when present.
        for arg in intercepted_args.testsuite_root:
            twister_cli.extend(["-T", arg])
    else:
        # Use EC base dir when no -T args specified. This will cause all
        # Twister-compatible EC tests to run.
        twister_cli.extend(["-T", str(ec_base)])
        twister_cli.extend(["-T", str(zephyr_base / "tests/subsys/shell")])

    # Pass through the chosen coverage tool, or fall back on the default choice
    # (see add_argument above).
    twister_cli.extend(
        [
            "--gcov-tool",
            intercepted_args.gcov_tool,
        ]
    )
    if intercepted_args.platform:
        # Pass user-provided -p args when present.
        for arg in intercepted_args.platform:
            twister_cli.extend(["-p", arg])
    else:
        # posix_native and unit_testing when nothing was requested by user.
        twister_cli.extend(["-p", "native_posix"])
        twister_cli.extend(["-p", "unit_testing"])

    # Append additional user-supplied args
    twister_cli.extend(other_args)

    # Print exact CLI args and environment variables depending on verbosity.
    if intercepted_args.verbose > 0:
        print("Calling:", " ".join(shlex.quote(str(x)) for x in twister_cli))
        print(
            "With environment overrides:",
            " ".join(
                f"{name}={shlex.quote(val)}"
                for name, val in extra_env_vars.items()
            ),
        )
        sys.stdout.flush()

    # Invoke Twister and wait for it to exit.
    result = subprocess.run(twister_cli, env=twister_env, check=False)

    if result.returncode == 0:
        print("TEST EXECUTION SUCCESSFUL")
    else:
        print("TEST EXECUTION FAILED")

    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
