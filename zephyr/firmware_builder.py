#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Build and test all of the Zephyr boards.

This is the entry point for the custom firmware builder workflow recipe.
"""

import argparse
import multiprocessing
import pathlib
import subprocess
import sys
import zmake.project

# TODO(crbug/1181505): Code outside of chromite should not be importing from
# chromite.api.gen.  Import json_format after that so we get the matching one.
from chromite.api.gen.chromite.api import firmware_pb2
from google.protobuf import json_format

DEFAULT_BUNDLE_DIRECTORY = '/tmp/artifact_bundles'
DEFAULT_BUNDLE_METADATA_FILE = '/tmp/artifact_bundle_metadata'


def build(opts):
    """Builds all Zephyr firmware targets"""
    metric_list = firmware_pb2.FwBuildMetricList()

    cmd = ['zmake', '-D', 'build', '-a']
    if opts.code_coverage:
        cmd.append('--coverage')
    subprocess.run(cmd, cwd=pathlib.Path(__file__).parent, check=True)
    if not opts.code_coverage:
        zephyr_dir = pathlib.Path(__file__).parent
        platform_ec = zephyr_dir.resolve().parent
        for project in zmake.project.find_projects(zephyr_dir).values():
            if project.config.is_test:
                continue
            build_dir = (
                platform_ec / 'build' / 'zephyr' / project.config.project_name
            )
            metric = metric_list.value.add()
            metric.target_name = project.config.project_name
            metric.platform_name = project.config.zephyr_board
            for (variant, _) in project.iter_builds():
                build_log = build_dir / f'build-{variant}' / 'build.log'
                parse_buildlog(build_log, metric, variant.upper())
    with open(opts.metrics, 'w') as f:
        f.write(json_format.MessageToJson(metric_list))
    return 0


UNITS = {
    'B': 1,
    'KB': 1024,
    'MB': 1024 * 1024,
    'GB': 1024 * 1024 * 1024,
}


def parse_buildlog(filename, metric, variant):
    with open(filename, 'r') as infile:
        # Skip over all lines until the memory report is found
        while True:
            line = infile.readline()
            if not line:
                return
            if line.startswith('Memory region'):
                break

        for line in infile.readlines():
            # Skip any lines that are not part of the report
            if not line.startswith(' '):
                continue
            parts = line.split()
            fw_section = metric.fw_section.add()
            fw_section.region = variant + '_' + parts[0][:-1]
            fw_section.used = int(parts[1]) * UNITS[parts[2]]
            fw_section.total = int(parts[3]) * UNITS[parts[4]]
            fw_section.track_on_gerrit = False


def bundle(opts):
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
    bundle_dir = pathlib.Path(bundle_dir)
    if not bundle_dir.is_dir():
        bundle_dir.mkdir()
    return bundle_dir


def write_metadata(opts, info):
    """Write the metadata about the bundle."""
    bundle_metadata_file = (
        opts.metadata if opts.metadata else DEFAULT_BUNDLE_METADATA_FILE
    )
    with open(bundle_metadata_file, 'w') as f:
        f.write(json_format.MessageToJson(info))


def bundle_coverage(opts):
    """Bundles the artifacts from code coverage into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    zephyr_dir = pathlib.Path(__file__).parent
    platform_ec = zephyr_dir.resolve().parent
    build_dir = platform_ec / 'build' / 'zephyr'
    tarball_name = 'coverage.tbz2'
    tarball_path = bundle_dir / tarball_name
    cmd = ['tar', 'cvfj', tarball_path, 'lcov.info']
    subprocess.run(cmd, cwd=build_dir, check=True)
    meta = info.objects.add()
    meta.file_name = tarball_name
    meta.lcov_info.type = (
        firmware_pb2.FirmwareArtifactInfo.LcovTarballInfo.LcovType.LCOV
    )

    write_metadata(opts, info)


def bundle_firmware(opts):
    """Bundles the artifacts from each target into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    zephyr_dir = pathlib.Path(__file__).parent
    platform_ec = zephyr_dir.resolve().parent
    for project in zmake.project.find_projects(zephyr_dir).values():
        if project.config.is_test:
            continue
        build_dir = (
            platform_ec / 'build' / 'zephyr' / project.config.project_name
        )
        artifacts_dir = build_dir / 'output'
        tarball_name = f'{project.config.project_name}.firmware.tbz2'
        tarball_path = bundle_dir.joinpath(tarball_name)
        cmd = ['tar', 'cvfj', tarball_path, '.']
        subprocess.run(cmd, cwd=artifacts_dir, check=True)
        meta = info.objects.add()
        meta.file_name = tarball_name
        meta.tarball_info.type = (
            firmware_pb2.FirmwareArtifactInfo.TarballInfo.FirmwareType.EC
        )
        # TODO(kmshelton): Populate the rest of metadata contents as it
        # gets defined in infra/proto/src/chromite/api/firmware.proto.

    write_metadata(opts, info)


def test(opts):
    """Runs all of the unit tests for Zephyr firmware"""
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwTestMetricList()
    with open(opts.metrics, 'w') as f:
        f.write(json_format.MessageToJson(metrics))

    zephyr_dir = pathlib.Path(__file__).parent.resolve()

    # Run zmake tests to ensure we have a fully working zmake before
    # proceeding.
    subprocess.run([zephyr_dir / 'zmake' / 'run_tests.sh'], check=True)

    # Run formatting checks on all BUILD.py files.
    config_files = zephyr_dir.rglob('**/BUILD.py')
    subprocess.run(['black', '--diff', '--check', *config_files], check=True)

    cmd = ['zmake', '-D', 'test', '-a', '--no-rebuild']
    if opts.code_coverage:
        cmd.append('--coverage')
    rv = subprocess.run(cmd, check=True).returncode
    if rv:
        return rv
    if opts.code_coverage:
        platform_ec = zephyr_dir.parent
        build_dir = platform_ec / 'build' / 'zephyr'
        # Merge lcov files here because bundle failures are "infra" failures.
        cmd = [
            '/usr/bin/lcov',
            '-o',
            build_dir / 'fullpaths.info',
            '--rc',
            'lcov_branch_coverage=1',
            '-a',
            build_dir / 'all_tests.info',
            '-a',
            build_dir / 'all_builds.info',
        ]
        subprocess.run(cmd, cwd=pathlib.Path(__file__).parent, check=True)
        # Make filenames relative to platform/ec
        cmd = ['sed', '-e', 's|^SF:.*/platform/ec/|SF:|']
        with open(build_dir / 'fullpaths.info') as infile, open(
                build_dir / 'lcov.info', 'w'
        ) as outfile:
            subprocess.run(
                cmd,
                cwd=pathlib.Path(__file__).parent,
                stdin=infile,
                stdout=outfile,
                check=True,
            )

    return 0


def main(args):
    """Builds and tests all of the Zephyr targets and reports build metrics"""
    opts = parse_args(args)

    if not hasattr(opts, 'func'):
        print('Must select a valid sub command!')
        return -1

    # Run selected sub command function
    return opts.func(opts)


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
        help=(
            'Full pathname for the file in which to write build artifact '
            'metadata.'
        ),
    )

    parser.add_argument(
        '--output-dir',
        required=False,
        help=(
            'Full pathname for the directory in which to bundle build '
            'artifacts.',
        )
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

    build_cmd = sub_cmds.add_parser('build', help='Builds all firmware targets')
    build_cmd.set_defaults(func=build)

    build_cmd = sub_cmds.add_parser(
        'bundle',
        help='Creates a tarball containing build '
        'artifacts from all firmware targets',
    )
    build_cmd.set_defaults(func=bundle)

    test_cmd = sub_cmds.add_parser('test', help='Runs all firmware unit tests')
    test_cmd.set_defaults(func=test)

    return parser.parse_args(args)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
