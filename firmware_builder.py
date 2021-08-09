#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Build, bundle, or test all of the EC boards.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import argparse
import multiprocessing
import os
import subprocess
import sys

# pylint: disable=import-error
from google.protobuf import json_format
# TODO(crbug/1181505): Code outside of chromite should not be importing from
# chromite.api.gen.  Import json_format after that so we get the matching one.
from chromite.api.gen.chromite.api import firmware_pb2


DEFAULT_BUNDLE_DIRECTORY = '/tmp/artifact_bundles'
DEFAULT_BUNDLE_METADATA_FILE = '/tmp/artifact_bundle_metadata'

# The the list of boards whose on-device unit tests we will verify compilation.
# TODO(b/172501728) On-device unit tests should build for all boards, but
# they've bit rotted, so we only build the ones that compile.
BOARDS_UNIT_TEST = [
    'bloonchipper',
    'dartmonkey',
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
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwBuildMetricList()
    with open(opts.metrics, 'w') as f:
        f.write(json_format.MessageToJson(metrics))

    if opts.code_coverage:
        print("When --code-coverage is selected, 'build' is a no-op. "
              "Run 'test' with --code-coverage instead.")
        return

    cmd = ['make', 'buildall_only', '-j{}'.format(opts.cpus)]
    print(f'# Running {" ".join(cmd)}.')
    subprocess.run(cmd,
                   cwd=os.path.dirname(__file__),
                   check=True)


def bundle(opts):
    if opts.code_coverage:
        bundle_coverage(opts)
    else:
        bundle_firmware(opts)


def get_bundle_dir(opts):
    """Get the directory for the bundle from opts or use the default.

    Also create the directory if it doesn't exist.
    """
    bundle_dir = opts.output_dir if opts.output_dir else \
        DEFAULT_BUNDLE_DIRECTORY
    if not os.path.isdir(bundle_dir):
        os.mkdir(bundle_dir)
    return bundle_dir


def write_metadata(opts, info):
    """Write the metadata about the bundle."""
    bundle_metadata_file = opts.metadata if opts.metadata else \
        DEFAULT_BUNDLE_METADATA_FILE
    with open(bundle_metadata_file, 'w') as f:
        f.write(json_format.MessageToJson(info))


def bundle_coverage(opts):
    """Bundles the artifacts from code coverage into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    tarball_name = 'coverage.tbz2'
    tarball_path = os.path.join(bundle_dir, tarball_name)
    cmd = ['tar', 'cvfj', tarball_path, 'lcov.info']
    subprocess.run(cmd, cwd=os.path.join(ec_dir, 'build/coverage'), check=True)
    meta = info.objects.add()
    meta.file_name = tarball_name
    meta.lcov_info.type = (
        firmware_pb2.FirmwareArtifactInfo.LcovTarballInfo.LcovType.LCOV)

    write_metadata(opts, info)


def bundle_firmware(opts):
    """Bundles the artifacts from each target into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    for build_target in sorted(os.listdir(os.path.join(ec_dir, 'build'))):
        tarball_name = ''.join([build_target, '.firmware.tbz2'])
        tarball_path = os.path.join(bundle_dir, tarball_name)
        cmd = [
            'tar', 'cvfj', tarball_path, '--exclude=*.o.d', '--exclude=*.o', '.'
        ]
        subprocess.run(
            cmd, cwd=os.path.join(ec_dir, 'build', build_target), check=True)
        meta = info.objects.add()
        meta.file_name = tarball_name
        meta.tarball_info.type = (
            firmware_pb2.FirmwareArtifactInfo.TarballInfo.FirmwareType.EC)
        # TODO(kmshelton): Populate the rest of metadata contents as it gets
        # defined in infra/proto/src/chromite/api/firmware.proto.

    write_metadata(opts, info)


def test(opts):
    """Runs all of the unit tests for EC firmware"""
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwTestMetricList()
    with open(opts.metrics, 'w') as f:
        f.write(json_format.MessageToJson(metrics))

    # If building for code coverage, build the 'coverage' target, which
    # builds the posix-based unit tests for code coverage and assembles
    # the LCOV information.
    #
    # Otherwise, build the 'runtests' target, which verifies all
    # posix-based unit tests build and pass.
    target = 'coverage' if opts.code_coverage else 'runtests'
    cmd = ['make', target, '-j{}'.format(opts.cpus)]
    print(f'# Running {" ".join(cmd)}.')
    subprocess.run(cmd,
                   cwd=os.path.dirname(__file__),
                   check=True)

    if not opts.code_coverage:
        # Verify compilation of the on-device unit test binaries.
        # TODO(b/172501728) These should build  for all boards, but they've bit
        # rotted, so we only build the ones that compile.
        cmd = ['make', '-j{}'.format(opts.cpus)]
        cmd.extend(['tests-' + b for b in BOARDS_UNIT_TEST])
        print(f'# Running {" ".join(cmd)}.')
        subprocess.run(cmd,
                       cwd=os.path.dirname(__file__),
                       check=True)


def main(args):
    """Builds, bundles, or tests all of the EC targets.

    Additionally, the tool reports build metrics.
    """
    opts = parse_args(args)

    if not hasattr(opts, 'func'):
        print('Must select a valid sub command!')
        return -1

    # Run selected sub command function
    try:
        opts.func(opts)
    except subprocess.CalledProcessError:
        return 1
    else:
        return 0


def parse_args(args):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        '--cpus',
        default=multiprocessing.cpu_count(),
        help='The number of cores to use.',
    )

    parser.add_argument(
        '--metrics',
        dest='metrics',
        required=True,
        help='File to write the json-encoded MetricsList proto message.',
    )

    parser.add_argument(
        '--metadata',
        required=False,
        help='Full pathname for the file in which to write build artifact '
        'metadata.',
    )

    parser.add_argument(
        '--output-dir',
        required=False,
        help='Full pathanme for the directory in which to bundle build '
        'artifacts.',
    )

    parser.add_argument(
        '--code-coverage',
        required=False,
        action='store_true',
        help='Build host-based unit tests for code coverage.',
    )

    parser.add_argument(
        '--bcs-version',
        dest='bcs_version',
        default='',
        required=False,
        # TODO(b/180008931): make this required=True.
        help='BCS version to include in metadata.',
    )

    # Would make this required=True, but not available until 3.7
    sub_cmds = parser.add_subparsers()

    build_cmd = sub_cmds.add_parser('build',
                                    help='Builds all firmware targets')
    build_cmd.set_defaults(func=build)

    build_cmd = sub_cmds.add_parser('bundle',
                                    help='Creates a tarball containing build '
                                    'artifacts from all firmware targets')
    build_cmd.set_defaults(func=bundle)

    test_cmd = sub_cmds.add_parser('test', help='Runs all firmware unit tests')
    test_cmd.set_defaults(func=test)

    return parser.parse_args(args)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
