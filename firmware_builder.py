#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Build and test all of the EC boards.

This is the entry point for the custom firmware builder workflow recipe.
"""

import argparse
import multiprocessing
import os
import subprocess
import sys

from google.protobuf import json_format

from chromite.api.gen.chromite.api import firmware_pb2

def build(opts):
    """Builds all EC firmware targets"""
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwBuildMetricList()
    with open(opts.metrics, 'w') as f:
        f.write(json_format.MessageToJson(metrics))
    return subprocess.run(['make', 'buildall_only', '-j{}'.format(opts.cpus)],
                          cwd=os.path.dirname(__file__)).returncode


def test(opts):
    """Runs all of the unit tests for EC firmware"""
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwTestMetricList()
    with open(opts.metrics, 'w') as f:
        f.write(json_format.MessageToJson(metrics))
    return subprocess.run(['make', 'runtests', '-j{}'.format(opts.cpus)],
                          cwd=os.path.dirname(__file__)).returncode


def main(args):
    """Builds and tests all of the EC targets and reports build metrics"""
    opts = parse_args(args)

    if not hasattr(opts, 'func'):
        print("Must select a valid sub command!")
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

    # Would make this required=True, but not available until 3.7
    sub_cmds = parser.add_subparsers()

    build_cmd = sub_cmds.add_parser('build',
                                    help='Builds all firmware targets')
    build_cmd.set_defaults(func=build)

    test_cmd = sub_cmds.add_parser('test', help='Runs all firmware unit tests')
    test_cmd.set_defaults(func=test)

    return parser.parse_args(args)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
