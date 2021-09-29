# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Module for project config wrapper object."""

import logging
import pathlib
import warnings

import yaml

import zmake.build_config as build_config
import zmake.modules as modules
import zmake.output_packers as packers
import zmake.toolchains as toolchains
import zmake.util as util

# The version of jsonschema in the chroot has a bunch of
# DeprecationWarnings that fire when we import it.  Suppress these
# during the import to keep the noise down.
with warnings.catch_warnings():
    warnings.simplefilter("ignore")
    import jsonschema


def module_dts_overlay_name(modpath, board_name):
    """Given a board name, return the expected DTS overlay path.

    Args:
        modpath: the module path as a pathlib.Path object
        board_name: the name of the board

    Returns:
        A pathlib.Path object to the expected overlay path.
    """
    return modpath / "zephyr" / "dts" / "board-overlays" / "{}.dts".format(board_name)


def find_projects(root_dir):
    """Finds all zmake projects in root_dir.

    Args:
        root_dir: the root dir as a pathlib.Path object

    Yields:
        Project: The next project found.
    """
    logging.info("Finding zmake targets under '%s'.", root_dir)
    for path in pathlib.Path(root_dir).rglob("zmake.yaml"):
        yield Project(path.parent)


class ProjectConfig:
    """An object wrapping zmake.yaml."""

    validator = jsonschema.Draft7Validator
    schema = {
        "type": "object",
        "required": [
            "board",
            "output-type",
            "supported-toolchains",
            "supported-zephyr-versions",
        ],
        "properties": {
            "supported-zephyr-versions": {
                "type": "array",
                "items": {
                    "type": "string",
                    "enum": ["v2.6", "v2.7", "v2.8"],
                },
                "minItems": 1,
                "uniqueItems": True,
            },
            "board": {
                "type": "string",
            },
            "modules": {
                "type": "array",
                "items": {
                    "type": "string",
                    "enum": list(modules.known_modules),
                },
            },
            "output-type": {
                "type": "string",
                "enum": list(packers.packer_registry),
            },
            "supported-toolchains": {
                "type": "array",
                "items": {
                    "type": "string",
                    "enum": list(toolchains.support_classes),
                },
            },
            "is-test": {
                "type": "boolean",
            },
            "dts-overlays": {
                "type": "array",
                "items": {
                    "type": "string",
                },
            },
        },
    }

    def __init__(self, config_dict):
        self.validator.check_schema(self.schema)
        jsonschema.validate(config_dict, self.schema, cls=self.validator)
        self.config_dict = config_dict

    @property
    def supported_zephyr_versions(self):
        return [
            util.parse_zephyr_version(x)
            for x in self.config_dict["supported-zephyr-versions"]
        ]

    @property
    def board(self):
        return self.config_dict["board"]

    @property
    def modules(self):
        return self.config_dict.get("modules", list(modules.known_modules))

    @property
    def output_packer(self):
        return packers.packer_registry[self.config_dict["output-type"]]

    @property
    def supported_toolchains(self):
        return self.config_dict["supported-toolchains"]

    @property
    def is_test(self):
        return self.config_dict.get("is-test", False)

    @property
    def dts_overlays(self):
        return self.config_dict.get("dts-overlays", [])


class Project:
    """An object encapsulating a project directory."""

    def __init__(self, project_dir, config_dict=None):
        self.project_dir = project_dir.resolve()
        if not config_dict:
            with open(self.project_dir / "zmake.yaml") as f:
                config_dict = yaml.safe_load(f)
        self.config = ProjectConfig(config_dict)
        self.packer = self.config.output_packer(self)

    def iter_builds(self):
        """Iterate thru the build combinations provided by the project's packer.

        Yields:
            2-tuples of a build configuration name and a BuildConfig.
        """
        conf = build_config.BuildConfig(cmake_defs={"BOARD": self.config.board})
        prj_conf = self.project_dir / "prj.conf"
        if prj_conf.is_file():
            conf |= build_config.BuildConfig(kconfig_files=[prj_conf])
        for build_name, packer_config in self.packer.configs():
            yield build_name, conf | packer_config

    def find_dts_overlays(self, modules):
        """Find appropriate dts overlays from registered modules.

        Args:
            modules: A dictionary of module names mapping to paths.

        Returns:
            A BuildConfig with relevant configurations to enable the
            found DTS overlay files.
        """
        overlays = []
        for module_path in modules.values():
            dts_path = module_dts_overlay_name(module_path, self.config.board)
            if dts_path.is_file():
                overlays.append(dts_path.resolve())

        overlays.extend(self.project_dir / f for f in self.config.dts_overlays)

        if overlays:
            return build_config.BuildConfig(
                cmake_defs={"DTC_OVERLAY_FILE": ";".join(map(str, overlays))}
            )
        else:
            return build_config.BuildConfig()

    def prune_modules(self, module_paths):
        """Reduce a modules dict to the ones required by this project.

        If this project does not define a modules list in the
        configuration, it is assumed that all known modules to Zmake
        are required.  This is typically inconsequential as Zephyr
        module design conventions require a Kconfig option to actually
        enable most modules.

        Args:
            module_paths: A dictionary mapping module names to their
                paths.  This dictionary is not modified.

        Returns:
            A new module_paths dictionary with only the modules
            required by this project.

        Raises:
            A KeyError, if a required module is unavailable.
        """
        result = {}
        for module in self.config.modules:
            try:
                result[module] = module_paths[module]
            except KeyError as e:
                raise KeyError(
                    "The {!r} module is required by the {} project, but is not "
                    "available.".format(module, self.project_dir)
                ) from e
        return result

    def get_toolchain(self, module_paths, override=None):
        if override:
            if override not in self.config.supported_toolchains:
                logging.warning(
                    "Toolchain %r isn't supported by this project. You're on your own.",
                    override,
                )
            support_class = toolchains.support_classes.get(
                override, toolchains.GenericToolchain
            )
            return support_class(name=override, modules=module_paths)
        else:
            for name in self.config.supported_toolchains:
                support_class = toolchains.support_classes[name]
                toolchain = support_class(name=name, modules=module_paths)
                if toolchain.probe():
                    logging.info("Toolchain %r selected by probe function.", toolchain)
                    return toolchain
            raise OSError(
                "No supported toolchains could be found on your system. If you see "
                "this message in the chroot, it indicates a bug. Otherwise, you'll "
                "either want to setup your system with a supported toolchain, or "
                "manually select an unsupported toolchain with the -t flag."
            )
