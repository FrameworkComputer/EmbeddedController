#!/usr/bin/env vpython3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper for invoking Twister

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
#   name: "infra/python/wheels/natsort-py3"
#   version: "version:8.3.1"
# >
# wheel: <
#   name: "infra/python/wheels/packaging-py2_py3"
#   version: "version:16.8"
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
#   name: "infra/python/wheels/pyelftools-py2_py3"
#   version: "version:0.29"
# >
# wheel: <
#   name: "infra/python/wheels/pykwalify-py2_py3"
#   version: "version:1.8.0"
# >
# wheel: <
#   name: "infra/python/wheels/pyparsing-py3"
#   version: "version:3.0.7"
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
# wheel: <
#   name: "infra/python/wheels/west-py3"
#   version: "version:0.14.0"
# >
# [VPYTHON:END]

import argparse
import hashlib
import os
import pathlib
from pathlib import Path
import re
import shlex
import shutil
from shutil import which
import socket
import subprocess
import sys
import tempfile
import time
from typing import List
import uuid


# Paths under the EC base dir that contain tests. This is used to define
# Twister's search scope.
EC_TEST_PATHS = [
    Path("common"),
    Path("zephyr/test"),
]

# Paths under ZEPHYR_BASE that we also wish to search for test cases.
ZEPHYR_TEST_PATHS = [
    Path("tests/drivers/fuel_gauge/sbs_gauge"),
    Path("tests/drivers/gpio"),
    Path("tests/subsys/shell"),
]


def find_checkout() -> Path:
    """Find the location of the source checkout or return None."""
    cros_checkout = os.environ.get("CROS_WORKON_SRCROOT")
    if cros_checkout is not None:
        return Path(cros_checkout)

    # Attempt to locate checkout location relatively if being run outside of
    # chroot.
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
        try:
            zephyr_base = Path(os.environ["ZEPHYR_BASE"]).resolve()
        except KeyError:
            zephyr_base = (
                cros_checkout / "src" / "third_party" / "zephyr" / "main"
            )
        zephyr_modules_dir = cros_checkout / "src" / "third_party" / "zephyr"
        pigweed_dir = cros_checkout / "src" / "third_party" / "pigweed"
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

        try:
            pigweed_dir = Path(os.environ["PIGWEED_DIR"]).resolve()
        except KeyError as err:
            raise RuntimeError(
                "PIGWEED_DIR unspecified. Please pass as env var to use chroot."
            ) from err

    return ec_base, zephyr_base, zephyr_modules_dir, pigweed_dir


def find_modules(mod_dir: Path) -> list:
    """Find Zephyr modules in the given directory `dir`."""

    modules = []
    for child in mod_dir.iterdir():
        if child.is_dir() and (child / "zephyr" / "module.yml").exists():
            modules.append(child.resolve())
    return modules


def is_tool(name):
    """Check if 'name' is on PATH and marked executable."""
    return which(name) is not None


def is_rdb_login():
    """Checks if user is logged into rdb"""
    cmd = ["rdb", "auth-info"]
    ret = subprocess.run(cmd, capture_output=True, text=True, check=False)

    if ret.returncode == 0:
        print("\nrdb auth-info: " + ret.stdout.split("\n")[0])
    else:
        print("\nrdb auth-info: " + ret.stderr)

    return ret.returncode == 0


def upload_results(ec_base, outdir):
    """Uploads Zephyr Test results to ResultDB"""
    flag = False

    if is_rdb_login():
        json_path = pathlib.Path(outdir) / "twister.json"

        if json_path.exists():
            hostname = socket.gethostname().split(".")[0]
            cmd = [
                "rdb",
                "stream",
                "-new",
                "-realm",
                "chromium:public",
                "-tag",
                "builder_name:" + hostname,
                "--",
                "vpython3",
                str(ec_base / "util/zephyr_to_resultdb.py"),
                "--result=" + str(json_path),
                "--upload=True",
            ]

            start_time = time.time()
            ret = subprocess.run(
                cmd, capture_output=True, text=True, check=True
            )
            end_time = time.time()

            # Extract URL to test report from captured output
            rdb_url = re.search(
                r"(?P<url>https?://[^\s]+)", ret.stderr.split("\n")[0]
            ).group("url")
            print(f"\nTEST RESULTS ({end_time - start_time:.3f}s): {rdb_url}\n")
            flag = ret.returncode == 0
    else:
        print("Unable to upload test results, please run 'rdb auth-login'\n")

    return flag


def append_cmake_compiler(cmdline, cmake_var, exe_options):
    """Picks the first available exe from exe_options and adds a cmake variable
    to cmdline."""
    for exe in exe_options:
        exe_path = shutil.which(exe)
        if exe_path:
            cmdline.append(f"-x={cmake_var}={exe_path}")
            return


def in_cros_sdk() -> bool:
    """Detect if the development environment is CrOS SDK.

    Returns:
        True if the environment looks like Firmware SDK, False otherwise.
    """
    return Path("/etc/cros_chroot_version").is_file()


def running_in_bazel() -> bool:
    """Detect if we're running as inside of a Bazel build already.

    Returns:
        True if it looks like we're running from a Bazel rule, False otherwise.
    """
    return "BAZEL_TWISTER_CWD" in os.environ


def in_fwsdk() -> bool:
    """Detect if the development environment is Firmware SDK.

    Returns:
        True if the environment looks like Firmware SDK, False otherwise.
    """
    # CrOS SDK cannot be FWSDK.  Check early and stop to prevent us from
    # breaking anything in CrOS SDK.
    if in_cros_sdk():
        return False

    # If we're already re-launched under Bazel, we're in Firmware SDK.
    if running_in_bazel():
        return True

    # Otherwise, ask Bazel to see if it knows about Firmware SDK rules.
    try:
        subprocess.run(
            [
                "bazel",
                "query",
                "//platform/rules_cros_firmware/cros_firmware:*",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False
    return True


def maybe_relaunch_in_bazel(
    argv: List[str], cwd: Path, sandbox_debug: bool = False
):
    """If Firmware SDK environment is detected, relaunch in Bazel.

    Args:
        argv: The command line to re-launch with.
        cwd: The directory to run inside.
        sandbox_debug: If True, run bazel with --sandbox_debug.
    """
    if not in_fwsdk():
        return False

    if running_in_bazel():
        return False

    # We want to parse args specially handled in fwsdk
    parser = argparse.ArgumentParser(add_help=False)

    # We intercept build-only/test-only because bazel should handle whether we build or not
    parser.add_argument("--test-only", action="store_true")
    parser.add_argument("-b", "--build-only", action="store_true")
    parser.add_argument("--prep-artifacts-for-testing", action="store_true")
    # We intercept testsuite-root to resolve paths.
    parser.add_argument("-T", "--testsuite-root", action="append")

    known_args, unknown_args = parser.parse_known_args(args=argv)
    args_from_fwsdk = []

    if known_args.test_only:
        print(
            "--test-only is not compatible with Bazel twister_launcher, remove it.",
            file=sys.stderr,
        )
        sys.exit(22)  # UNIX Invalid Argument error code

    if known_args.testsuite_root:
        for arg in known_args.testsuite_root:
            args_from_fwsdk.extend(["-T", str(Path(arg).resolve())])

    argv = args_from_fwsdk + unknown_args

    gen_starlark = f"""
load(
    "//platform/ec/bazel:twister.bzl",
    "twister_test",
)
twister_test(
    name = "run_twister",
    args = {argv!r},
    cwd = {str(cwd)!r},
)"""
    run_hash = hashlib.md5(gen_starlark.encode("utf-8")).hexdigest()
    build_dir = Path(__file__).resolve().parent.parent / "build"
    twister_bzl_dir = build_dir / "twister-bzl"
    run_dir = twister_bzl_dir / run_hash

    if not run_dir.is_dir():
        run_dir.mkdir(parents=True)
        (run_dir / "BUILD.bazel").write_text(gen_starlark, encoding="utf-8")

    # Twister users are used to seeing `twister-out`; symlink it to bazel twister-out
    bazel_bin = Path(
        subprocess.run(
            ["bazel", "info", "bazel-bin"],
            cwd=Path(__file__).resolve().parent,
            check=True,
            stdout=subprocess.PIPE,
            encoding="utf-8",
        ).stdout.strip()
    )

    ec_twister_out = Path("twister-out_build")
    bazel_twister_out = (
        # TODO b/293119619: Create symlink for test execution output too.
        bazel_bin
        / "platform/ec/build/twister-bzl"
        / run_hash
        / "twister-out_build"
    )

    # Atomically symlink to twister out/build directory
    # Largely taken from chromite.osutils.SafeSymlink()
    tmp_twister_out = ec_twister_out.with_name(
        f".tmp-{ec_twister_out.name}-{uuid.uuid4().hex}"
    )
    try:
        tmp_twister_out.symlink_to(bazel_twister_out)
        tmp_twister_out.rename(ec_twister_out)
    except OSError:
        # Clean up temporary symlink if rename failed
        tmp_twister_out.unlink(missing_ok=True)

    # Bazel likes to cache test executions, we're not interested in this behavior.
    bazel_cmd = ["bazel", "test", ":run_twister"]

    bazel_cmd.append("--test_output=all")
    # Bazel likes to cache test results in addition to builds.
    bazel_cmd.append("--nocache_test_results")
    # By default be as verbose as twister is.
    bazel_cmd.append("--test_output=streamed")
    if sandbox_debug:
        bazel_cmd.append("--sandbox_debug")

    result = subprocess.run(bazel_cmd, cwd=run_dir, check=False)
    sys.exit(result.returncode)


def main():
    """Run Twister using defaults for the EC project."""

    bazel_outdir = None
    if "BAZEL_TWISTER_CWD" in os.environ:
        # Resolve any paths relative to the execroot prior to changing
        # directories.
        bazel_outdir = Path(os.environ["BAZEL_TWISTER_OUTDIR"]).resolve()
        os.environ["TOOLCHAIN_ROOT"] = str(
            Path(os.environ["TOOLCHAIN_ROOT"]).resolve()
        )
        os.chdir(os.environ["BAZEL_TWISTER_CWD"])

    # Get paths for the build.
    ec_base, zephyr_base, zephyr_modules_dir, pigweed_dir = find_paths()

    zephyr_base = zephyr_base.resolve()
    zephyr_modules = find_modules(zephyr_modules_dir)

    # Add the EC dir as a module if not already included (resolve all paths to
    # account for symlinked or relative paths)
    if ec_base.resolve() not in zephyr_modules:
        zephyr_modules.append(ec_base)

    if pigweed_dir.resolve() not in zephyr_modules:
        zephyr_modules.append(pigweed_dir)

    # Twister CLI args
    twister_cli = [
        sys.executable,
        str(zephyr_base / "scripts" / "twister"),  # Executable path
        "--ninja",
        f"-x=DTS_ROOT={str( ec_base / 'zephyr')}",
        f"-x=SYSCALL_INCLUDE_DIRS={str(ec_base / 'zephyr' / 'include' / 'drivers')}",
        f"-x=ZEPHYR_BASE={zephyr_base}",
        f"-x=ZEPHYR_MODULES={';'.join([str(p) for p in zephyr_modules])}",
        f"-x=Python3_EXECUTABLE={sys.executable}",
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
    parser.add_argument("--gcov-tool")
    parser.add_argument(
        "--no-upload-cros-rdb", dest="upload_cros_rdb", action="store_false"
    )
    parser.add_argument(
        "-O",
        "--outdir",
        default=bazel_outdir or os.path.join(os.getcwd(), "twister-out"),
    )
    parser.add_argument(
        "--toolchain",
        default=os.environ.get(
            "ZEPHYR_TOOLCHAIN_VARIANT",
            "llvm" if in_cros_sdk() else "host",
        ),
    )
    parser.add_argument(
        "--gcc", dest="toolchain", action="store_const", const="host"
    )
    parser.add_argument(
        "--llvm",
        "--clang",
        dest="toolchain",
        action="store_const",
        const="llvm",
    )
    parser.add_argument(
        "--sandbox-debug",
        action="store_true",
        default=False,
        help="When running under Bazel, enable sandbox debugging mode.",
    )
    parser.add_argument(
        "-h",
        "--help",
        action="store_true",
    )
    parser.add_argument(
        "-C",
        "--coverage",
        action="store_true",
    )

    intercepted_args, other_args = parser.parse_known_args()

    # We allow help to run outside the sandbox.
    if intercepted_args.help:
        twister_cli.append("--help")
    else:
        maybe_relaunch_in_bazel(
            sys.argv[1:],
            Path.cwd(),
            sandbox_debug=intercepted_args.sandbox_debug,
        )

    # Zephyr upstream defaults to using gcovr, but this isn't
    # available in the chroot.  Force our runs to use lcov.
    if intercepted_args.coverage:
        twister_cli.append("--coverage")
        twister_cli.extend(["--coverage-tool", "lcov"])

    for _ in range(intercepted_args.verbose):
        # Pass verbosity setting through to twister
        twister_cli.append("-v")

    if intercepted_args.testsuite_root:
        # Pass user-provided -T args when present.
        for arg in intercepted_args.testsuite_root:
            twister_cli.extend(["-T", arg])
    else:
        # Use this set of test suite roots when no -T args are present. This
        # should encompass all current Zephyr EC tests. The paths are meant to
        # be as specific as possible to limit Twister's search scope. This saves
        # significant time when starting Twister.
        for path in EC_TEST_PATHS:
            twister_cli.extend(["-T", str(ec_base / path)])

        # Upstream tests we also wish to run:
        for path in ZEPHYR_TEST_PATHS:
            twister_cli.extend(["-T", str(zephyr_base / path)])

    if intercepted_args.platform:
        # Pass user-provided -p args when present.
        for arg in intercepted_args.platform:
            twister_cli.extend(["-p", arg])
    else:
        # posix_native and unit_testing when nothing was requested by user.
        twister_cli.extend(["-p", "native_posix"])
        twister_cli.extend(["-p", "unit_testing"])
        twister_cli.extend(["-p", "native_sim"])

    twister_cli.extend(["--outdir", intercepted_args.outdir])

    # Prepare environment variables for export to Twister. Inherit the parent
    # process's environment, but set some default values if not already set.
    twister_env = dict(os.environ)
    with tempfile.TemporaryDirectory() as parsetab_dir:
        toolchain_root = os.environ.get(
            "TOOLCHAIN_ROOT",
            str(ec_base / "zephyr") if in_cros_sdk() else str(zephyr_base),
        )

        twister_cli.extend([f"-x=TOOLCHAIN_ROOT={toolchain_root}"])
        extra_env_vars = {
            "TOOLCHAIN_ROOT": toolchain_root,
            # TODO(https://github.com/zephyrproject-rtos/zephyr/issues/59453):
            # This ought to be passed as a CMake variable but can't due to how
            # Zephyr calls verify-toolchain.cmake
            "ZEPHYR_TOOLCHAIN_VARIANT": intercepted_args.toolchain,
            "PARSETAB_DIR": parsetab_dir,
        }
        gcov_tool = None
        if intercepted_args.toolchain == "host":
            gcov_tool = "gcov"
            # Inside the chroot, the binutils is still at v2.36 and does not
            # support the -no-pie flag with the linker and generates warnings.
            # Disable warnings as errors for the GCC toolchain only.
            twister_cli.extend(["--disable-warnings-as-errors"])
        elif intercepted_args.toolchain == "llvm":
            gcov_tool = str(ec_base / "util" / "llvm-gcov.sh")
        else:
            print("Unknown toolchain specified:", intercepted_args.toolchain)
        if intercepted_args.gcov_tool:
            gcov_tool = intercepted_args.gcov_tool
        if gcov_tool:
            twister_cli.extend(["--gcov-tool", gcov_tool])

        twister_env.update(extra_env_vars)

        # Append additional user-supplied args
        twister_cli.extend(other_args)

        # Print exact CLI args and environment variables depending on verbosity.
        if intercepted_args.verbose > 0:
            print(
                "Calling:", " ".join(shlex.quote(str(x)) for x in twister_cli)
            )
            print(
                "With environment overrides:",
                " ".join(
                    f"{name}={shlex.quote(val)}"
                    for name, val in extra_env_vars.items()
                ),
            )
            sys.stdout.flush()

        # Invoke Twister and wait for it to exit.
        result = subprocess.run(
            twister_cli,
            env=twister_env,
            check=False,
            close_fds=False,  # For GNUMakefile jobserver
        )

        if result.returncode == 0:
            print("TEST EXECUTION SUCCESSFUL")
        else:
            print("TEST EXECUTION FAILED")

        # TODO(b/295197371): Add rdb support for test builds in fwsdk
        if is_tool("rdb") and intercepted_args.upload_cros_rdb:
            upload_results(ec_base, intercepted_args.outdir)

        sys.exit(result.returncode)


if __name__ == "__main__":
    main()
