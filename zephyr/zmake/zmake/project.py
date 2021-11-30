# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Module for project config wrapper object."""

import dataclasses
import logging
import pathlib

import zmake.build_config as build_config
import zmake.configlib as configlib
import zmake.modules as modules
import zmake.toolchains as toolchains


def module_dts_overlay_name(modpath, board_name):
    """Given a board name, return the expected DTS overlay path.

    Args:
        modpath: the module path as a pathlib.Path object
        board_name: the name of the board

    Returns:
        A pathlib.Path object to the expected overlay path.
    """
    return modpath / "zephyr" / "dts" / "board-overlays" / "{}.dts".format(board_name)


def load_config_file(path):
    """Load a BUILD.py config file and create associated projects.

    Args:
        path: A pathlib.Path to the BUILD.py file.

    Returns:
        A list of Project objects specified by the file.
    """
    projects = []

    def register_project(**kwargs):
        projects.append(Project(ProjectConfig(**kwargs)))

    # The Python environment passed to the config file.
    config_globals = {
        "register_project": register_project,
        "here": path.parent.resolve(),
    }

    # First, load the global helper functions.
    code = compile(
        pathlib.Path(configlib.__file__).read_bytes(),
        configlib.__file__,
        "exec",
    )
    exec(code, config_globals)

    # Next, load the BUILD.py
    logging.debug("Loading config file %s", path)
    code = compile(path.read_bytes(), str(path), "exec")
    exec(code, config_globals)
    logging.debug("Config file %s defines %s projects", path, len(projects))
    return projects


def find_projects(root_dir):
    """Finds all zmake projects in root_dir.

    Args:
        root_dir: the root dir as a pathlib.Path object

    Returns:
        A dictionary mapping project names to Project objects.
    """
    logging.debug("Finding zmake targets under '%s'.", root_dir)
    found_projects = {}
    for path in pathlib.Path(root_dir).rglob("BUILD.py"):
        for project in load_config_file(path):
            if project.config.project_name in found_projects:
                raise KeyError(
                    "Duplicate project defined: {} (in {})".format(
                        project.config.project_name, path
                    )
                )
            found_projects[project.config.project_name] = project
    return found_projects


@dataclasses.dataclass
class ProjectConfig:
    project_name: str
    zephyr_board: str
    supported_toolchains: "list[str]"
    output_packer: type
    modules: "list[str]" = dataclasses.field(
        default_factory=lambda: modules.known_modules,
    )
    is_test: bool = dataclasses.field(default=False)
    dts_overlays: "list[str]" = dataclasses.field(default_factory=list)
    kconfig_files: "list[pathlib.Path]" = dataclasses.field(default_factory=list)
    project_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)


class Project:
    """An object encapsulating a project directory."""

    def __init__(self, config):
        self.config = config
        self.packer = self.config.output_packer(self)

    def iter_builds(self):
        """Iterate thru the build combinations provided by the project's packer.

        Yields:
            2-tuples of a build configuration name and a BuildConfig.
        """
        conf = build_config.BuildConfig(cmake_defs={"BOARD": self.config.zephyr_board})

        kconfig_files = []
        prj_conf = self.config.project_dir / "prj.conf"
        if prj_conf.is_file():
            kconfig_files.append(prj_conf)
        kconfig_files.extend(self.config.kconfig_files)
        conf |= build_config.BuildConfig(kconfig_files=kconfig_files)

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
            dts_path = module_dts_overlay_name(module_path, self.config.zephyr_board)
            if dts_path.is_file():
                overlays.append(dts_path.resolve())

        overlays.extend(self.config.dts_overlays)

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
                    "available.".format(module, self.config.project_dir)
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
