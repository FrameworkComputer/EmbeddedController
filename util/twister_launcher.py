#!/usr/bin/env python3

# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
This script is a wrapper for invoking Twister, the Zephyr test runner, using
default parameters for the ChromiumOS EC project. For an overview of CLI
parameters that may be used, please consult the Twister documentation.
"""

import argparse
import os
import subprocess
from pathlib import Path


def find_checkout() -> Path:
    """Find the location of the source checkout or raise an error."""
    # cros_checkout = os.environ.get("CROS_WORKON_SRCROOT")
    # if cros_checkout is not None:
    #     return Path(cros_checkout)

    # Attempt to locate checkout location relatively if being run outside of chroot.
    try:
        cros_checkout = Path(__file__).resolve().parents[4]
        assert (cros_checkout / "src").exists()
        return cros_checkout
    except (IndexError, AssertionError) as err:
        raise RuntimeError(
            "Cannot locate the checkout source root. Try entering the chroot or "
            "making sure your directory structure matches it."
        ) from err


def find_modules(mod_dir: Path) -> list:
    """Find Zephyr modules in the given directory `dir`."""

    modules = []
    for child in mod_dir.iterdir():
        if child.is_dir() and (child / "zephyr" / "module.yml").exists():
            modules.append(child)
    return modules


def main():
    """Run Twister using defaults for the EC project."""

    # Determine where the source tree is checked out.
    cros_checkout = find_checkout()

    # Use ZEPHYR_BASE environment var, or compute from checkout path if not
    # specified.
    zephyr_base = Path(
        os.environ.get(
            "ZEPHYR_BASE",
            cros_checkout / "src" / "third_party" / "zephyr" / "main",
        )
    )

    ec_base = cros_checkout / "src" / "platform" / "ec"

    # Module paths, including third party modules and the EC application.
    zephyr_modules = find_modules(cros_checkout / "src" / "third_party" / "zephyr")
    zephyr_modules.append(ec_base)

    # Prepare environment variables for export to Twister and inherit the
    # parent environment.
    twister_env = dict(os.environ)
    twister_env.update(
        {
            "ZEPHYR_BASE": str(zephyr_base),
            "TOOLCHAIN_ROOT": str(zephyr_base),
            "ZEPHYR_TOOLCHAIN_VARIANT": "host",
        }
    )

    # Twister CLI args
    twister_cli = [
        str(zephyr_base / "scripts" / "twister"),  # Executable path
        "--ninja",
        f"-x=DTS_ROOT={str( ec_base / 'zephyr')}",
        f"-x=SYSCALL_INCLUDE_DIRS={str(ec_base / 'zephyr' / 'include' / 'drivers')}",
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
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("-T", "--testsuite-root", action="append")
    parser.add_argument("-v", "--verbose", action="count", default=0)
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

    # Append additional user-supplied args
    twister_cli.extend(other_args)

    # Print exact CLI args and environment variables depending on verbosity.
    if intercepted_args.verbose > 0:
        print("Calling:", twister_cli)
    if intercepted_args.verbose > 1:
        print("With environment:", twister_env)

    # Invoke Twister and wait for it to exit.
    with subprocess.Popen(
        twister_cli,
        env=twister_env,
    ) as proc:
        proc.wait()


if __name__ == "__main__":
    main()
