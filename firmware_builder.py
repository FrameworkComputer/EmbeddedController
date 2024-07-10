#!/usr/bin/env -S python3 -u
# -*- coding: utf-8 -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build, bundle, or test all of the EC boards.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import argparse
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys

# pylint: disable=import-error
from google.protobuf import json_format

from chromite.api.gen_sdk.chromite.api import firmware_pb2


DEFAULT_BUNDLE_DIRECTORY = "/tmp/artifact_bundles"
DEFAULT_BUNDLE_METADATA_FILE = "/tmp/artifact_bundle_metadata"

# The the list of boards whose on-device unit tests we will verify compilation.
# TODO(b/172501728) On-device unit tests should build for all boards, but
# they've bit rotted, so we only build the ones that compile.
BOARDS_UNIT_TEST = [
    "bloonchipper",
    "dartmonkey",
    "helipilot",
]

# Interesting regions to show in gerrit
BINARY_SIZE_REGIONS = ["RW_FLASH", "RW_IRAM"]

# The most recently edited boards that should show binary size changes in
# gerrit
BINARY_SIZE_BOARDS = [
    "dexi",
    "dibbi",
    "dita",
    "gaelin",
    "gladios",
    "lisbon",
    "marasov",
    "moli",
    "prism",
    "shotzo",
    "taranza",
]


def build(opts):
    """Builds all EC firmware targets

    Note that when we are building unit tests for code coverage, we don't
    need this step. It builds EC **firmware** targets, but unit tests with
    code coverage are all host-based. So if the --code-coverage flag is set,
    we don't need to build the firmware targets and we can return without
    doing anything but creating the metrics file and giving an informational
    message.
    """
    metric_list = firmware_pb2.FwBuildMetricList()  # pylint: disable=no-member
    ec_dir = pathlib.Path(__file__).parent

    # Run formatting checks on all python files.
    cmd = ["black", "--check", "."]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)
    chromite_dir = ec_dir.resolve().parent.parent.parent / "chromite"
    cmd = [
        "isort",
        f"--settings-file={chromite_dir / '.isort.cfg'}",
        "--check",
        "--gitignore",
        "--dont-follow-links",
        ".",
    ]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(
        cmd,
        cwd=os.path.dirname(__file__),
        check=True,
    )

    if opts.code_coverage:
        print(
            "When --code-coverage is selected, 'build' is a no-op. "
            "Run 'test' with --code-coverage instead."
        )
        with open(opts.metrics, "w", encoding="utf-8") as file:
            file.write(json_format.MessageToJson(metric_list))
        return

    cmd = ["make", "clobber"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    cmd = ["make", "buildall_only", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    # extra/rma_reset is used in chromeos-base/ec-utils-test
    cmd = ["make", "-C", "extra/rma_reset", "clean"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    cmd = ["make", "-C", "extra/rma_reset", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    # extra/usb_updater is used in chromeos-base/ec-devutils
    cmd = ["make", "-C", "extra/usb_updater", "clean"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    cmd = ["make", "-C", "extra/usb_updater", "usb_updater2", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    cmd = ["make", "print-all-baseboards", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    baseboards = {}
    for line in subprocess.run(
        cmd,
        cwd=os.path.dirname(__file__),
        check=True,
        universal_newlines=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines():
        parts = line.split("=")
        if len(parts) > 1:
            baseboards[parts[0]] = parts[1]

    ec_dir = os.path.dirname(__file__)
    build_dir = os.path.join(ec_dir, "build")
    for build_target in sorted(os.listdir(build_dir)):
        metric = metric_list.value.add()
        metric.target_name = build_target
        metric.platform_name = build_target
        if build_target in baseboards and baseboards[build_target]:
            metric.platform_name = baseboards[build_target]

        for variant in ["RO", "RW"]:
            memsize_file = (
                pathlib.Path(build_dir)
                / build_target
                / variant
                / f"ec.{variant}.elf.memsize.txt"
            )
            if memsize_file.exists():
                parse_memsize(
                    memsize_file,
                    metric,
                    variant,
                    build_target in BINARY_SIZE_BOARDS,
                )
    with open(opts.metrics, "w", encoding="utf-8") as file:
        file.write(json_format.MessageToJson(metric_list))

    gcc_build_dir = build_dir + ".gcc"
    try:
        # b/352025405: build_with_clang.py deletes the build directory, but
        # we want to preserve the gcc build artifacts for uploading (bundling).
        # Temporarily rename the gcc build output directory and restore after
        # the clang build finishes.
        os.rename(build_dir, gcc_build_dir)

        # Ensure that there are no regressions for boards that build
        # successfully with clang: b/172020503.
        cmd = ["./util/build_with_clang.py"]
        print(f'# Running {" ".join(cmd)}.')
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)
    finally:
        shutil.rmtree(build_dir)
        os.rename(gcc_build_dir, build_dir)


UNITS = {
    "B": 1,
    "KB": 1024,
    "MB": 1024 * 1024,
    "GB": 1024 * 1024 * 1024,
}


def parse_memsize(filename, metric, variant, track_on_gerrit):
    """Parse the output of the build to extract the image size."""
    with open(filename, "r", encoding="utf-8") as infile:
        # Skip header line
        infile.readline()
        for line in infile.readlines():
            parts = line.split()
            fw_section = metric.fw_section.add()
            fw_section.region = variant + "_" + parts[0][:-1]
            fw_section.used = int(parts[1]) * UNITS[parts[2]]
            fw_section.total = int(parts[3]) * UNITS[parts[4]]
            if track_on_gerrit and fw_section.region in BINARY_SIZE_REGIONS:
                fw_section.track_on_gerrit = True
            else:
                fw_section.track_on_gerrit = False


def bundle(opts):
    """Bundle the artifacts."""
    if opts.code_coverage:
        bundle_coverage(opts)
    else:
        bundle_firmware(opts)


def get_bundle_dir(opts):
    """Get the directory for the bundle from opts or use the default.

    Also create the directory if it doesn't exist.
    """
    if opts.output_dir:
        bundle_dir = opts.output_dir
    else:
        bundle_dir = DEFAULT_BUNDLE_DIRECTORY
    if not os.path.isdir(bundle_dir):
        os.mkdir(bundle_dir)
    return bundle_dir


def write_metadata(opts, info):
    """Write the metadata about the bundle."""
    bundle_metadata_file = (
        opts.metadata if opts.metadata else DEFAULT_BUNDLE_METADATA_FILE
    )
    with open(bundle_metadata_file, "w", encoding="utf-8") as file:
        file.write(json_format.MessageToJson(info))


def bundle_coverage(opts):
    """Bundles the artifacts from code coverage into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()  # pylint: disable=no-member
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    tarball_name = "coverage.tbz2"
    tarball_path = os.path.join(bundle_dir, tarball_name)
    cmd = ["tar", "cvfj", tarball_path, "lcov.info"]
    subprocess.run(cmd, cwd=os.path.join(ec_dir, "build/coverage"), check=True)
    meta = info.objects.add()
    meta.file_name = tarball_name
    meta.lcov_info.type = (
        firmware_pb2.FirmwareArtifactInfo.LcovTarballInfo.LcovType.LCOV  # pylint: disable=no-member
    )

    write_metadata(opts, info)


def bundle_firmware(opts):
    """Bundles the artifacts from each target into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()  # pylint: disable=no-member
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    for build_target in sorted(os.listdir(os.path.join(ec_dir, "build"))):
        if build_target in ["host"]:
            continue
        tarball_name = "".join([build_target, ".firmware.tbz2"])
        tarball_path = os.path.join(bundle_dir, tarball_name)
        cmd = [
            "tar",
            "cvfj",
            tarball_path,
            "--exclude=*.d",
            "--exclude=*.o",
            ".",
        ]
        subprocess.run(
            cmd,
            cwd=os.path.join(ec_dir, "build", build_target),
            check=True,
        )
        meta = info.objects.add()
        meta.file_name = tarball_name
        meta.tarball_info.type = (
            firmware_pb2.FirmwareArtifactInfo.TarballInfo.FirmwareType.EC  # pylint: disable=no-member
        )
        # TODO(kmshelton): Populate the rest of metadata contents as it gets
        # defined in infra/proto/src/chromite/api/firmware.proto.

    write_metadata(opts, info)


def test(opts):
    """Runs all of the unit tests for EC firmware"""
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwTestMetricList()  # pylint: disable=no-member
    with open(opts.metrics, "w", encoding="utf-8") as file:
        file.write(json_format.MessageToJson(metrics))

    # Run python unit tests.
    subprocess.run(
        ["extra/stack_analyzer/run_tests.sh"],
        cwd=os.path.dirname(__file__),
        check=True,
    )
    subprocess.run(
        ["util/run_tests.sh"], cwd=os.path.dirname(__file__), check=True
    )

    # If building for code coverage, build the 'coverage' target, which
    # builds the posix-based unit tests for code coverage and assembles
    # the LCOV information.
    #
    # Otherwise, build the 'runtests' target, which verifies all
    # posix-based unit tests build and pass.
    target = "coverage" if opts.code_coverage else "runtests"
    cmd = ["make", target, f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

    if not opts.code_coverage:
        # Verify compilation of the on-device unit test binaries.
        # TODO(b/172501728) These should build  for all boards, but they've bit
        # rotted, so we only build the ones that compile.
        cmd = ["make", f"-j{opts.cpus}"]
        cmd.extend(["tests-" + b for b in BOARDS_UNIT_TEST])
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

        # Verify the tests pass with ASan also
        ec_dir = os.path.dirname(__file__)
        build_dir = os.path.join(ec_dir, "build")
        host_dir = os.path.join(build_dir, "host")
        print(f"# Deleting {host_dir}")
        shutil.rmtree(host_dir)

        cmd = ["make", "TEST_ASAN=y", target, f"-j{opts.cpus}"]
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)

        # Use the x86_64-cros-linux-gnu- compiler also
        cmd = [
            "make",
            "clean",
            "HOST_CROSS_COMPILE=x86_64-cros-linux-gnu-",
            "TEST_ASAN=y",
            target,
            f"-j{opts.cpus}",
        ]
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True)


def main(args):
    """Builds, bundles, or tests all of the EC targets.

    Additionally, the tool reports build metrics.
    """
    opts = parse_args(args)

    if not hasattr(opts, "func"):
        print("Must select a valid sub command!")
        return -1

    # Run selected sub command function
    try:
        opts.func(opts)
    except subprocess.CalledProcessError:
        ec_dir = os.path.dirname(__file__)
        failed_dir = os.path.join(ec_dir, ".failedboards")
        if os.path.isdir(failed_dir):
            print("Failed boards/tests:")
            for fail in os.listdir(failed_dir):
                print(f"\t{fail}")
        return 1
    else:
        return 0


def parse_args(args):
    """Parse all command line args and return opts dict."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--cpus",
        default=multiprocessing.cpu_count(),
        help="The number of cores to use.",
    )

    parser.add_argument(
        "--metrics",
        dest="metrics",
        required=True,
        help="File to write the json-encoded MetricsList proto message.",
    )

    parser.add_argument(
        "--metadata",
        required=False,
        help="Full pathname for the file in which to write build artifact metadata.",
    )

    parser.add_argument(
        "--output-dir",
        required=False,
        help="Full pathanme for the directory in which to bundle build artifacts.",
    )

    parser.add_argument(
        "--code-coverage",
        required=False,
        action="store_true",
        help="Build host-based unit tests for code coverage.",
    )

    parser.add_argument(
        "--bcs-version",
        dest="bcs_version",
        default="",
        required=False,
        # TODO(b/180008931): make this required=True.
        help="BCS version to include in metadata.",
    )

    # Would make this required=True, but not available until 3.7
    sub_cmds = parser.add_subparsers()

    build_cmd = sub_cmds.add_parser("build", help="Builds all firmware targets")
    build_cmd.set_defaults(func=build)

    build_cmd = sub_cmds.add_parser(
        "bundle",
        help="Creates a tarball containing build artifacts from all firmware targets",
    )
    build_cmd.set_defaults(func=bundle)

    test_cmd = sub_cmds.add_parser("test", help="Runs all firmware unit tests")
    test_cmd.set_defaults(func=test)

    return parser.parse_args(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
