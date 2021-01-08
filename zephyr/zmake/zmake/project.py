# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Module for project config wrapper object."""

import jsonschema
import yaml

import zmake.build_config as build_config
import zmake.output_packers as packers
import zmake.util as util


class ProjectConfig:
    """An object wrapping zmake.yaml."""
    validator = jsonschema.Draft7Validator
    schema = {
        'type': 'object',
        'required': ['supported-zephyr-versions', 'board', 'output-type',
                     'toolchain'],
        'properties': {
            'supported-zephyr-versions': {
                'type': 'array',
                'items': {
                    'type': 'string',
                    'enum': ['v2.4'],
                },
                'minItems': 1,
                'uniqueItems': True,
            },
            'board': {
                'type': 'string',
            },
            'output-type': {
                'type': 'string',
                'enum': list(packers.packer_registry),
            },
            'toolchain': {
                'type': 'string',
            },
            'prefer-zephyr-sdk': {
                'type': 'boolean',
            },
            'is-test': {
                'type': 'boolean',
            },
        },
    }

    def __init__(self, config_dict):
        self.validator.check_schema(self.schema)
        jsonschema.validate(config_dict, self.schema, cls=self.validator)
        self.config_dict = config_dict

    @property
    def supported_zephyr_versions(self):
        return [util.parse_zephyr_version(x)
                for x in self.config_dict['supported-zephyr-versions']]

    @property
    def board(self):
        return self.config_dict['board']

    @property
    def output_packer(self):
        return packers.packer_registry[self.config_dict['output-type']]

    @property
    def toolchain(self):
        return self.config_dict['toolchain']

    @property
    def zephyr_sdk_is_preferred(self):
        return self.config_dict.get('prefer-zephyr-sdk', False)

    @property
    def is_test(self):
        return self.config_dict.get('is-test', False)


class Project:
    """An object encapsulating a project directory."""
    def __init__(self, project_dir):
        self.project_dir = project_dir.resolve()
        with open(self.project_dir / 'zmake.yaml') as f:
            self.config = ProjectConfig(yaml.safe_load(f))
        self.packer = self.config.output_packer(self)

    def iter_builds(self):
        """Iterate thru the build combinations provided by the project's packer.

        Yields:
            2-tuples of a build configuration name and a BuildConfig.
        """
        conf = build_config.BuildConfig(cmake_defs={'BOARD': self.config.board})
        if (self.project_dir / 'boards').is_dir():
            conf |= build_config.BuildConfig(
                cmake_defs={'BOARD_ROOT': str(self.project_dir)})
        prj_conf = self.project_dir / 'prj.conf'
        if prj_conf.is_file():
            conf |= build_config.BuildConfig(kconfig_files=[prj_conf])
        for build_name, packer_config in self.packer.configs():
            yield build_name, conf | packer_config
