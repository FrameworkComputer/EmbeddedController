# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for project config wrapper object."""

import dataclasses
import logging
import os
import pathlib
import typing

from zmake import build_config
from zmake import configlib
from zmake import signers
from zmake import toolchains
import zmake.modules
import zmake.output_packers


def module_dts_overlay_name(modpath, board_name):
    """Given a board name, return the expected DTS overlay path.

    Args:
        modpath: the module path as a pathlib.Path object
        board_name: the name of the board

    Returns:
        A pathlib.Path object to the expected overlay path.
    """
    return modpath / "zephyr" / "dts" / "board-overlays" / f"{board_name}.dts"


@dataclasses.dataclass
class ProjectConfig:
    """All the information needed to define a project."""

    # pylint: disable=too-many-instance-attributes
    project_name: str
    zephyr_board: str
    supported_toolchains: "list[str]"
    output_packer: type
    modules: typing.Iterable[str] = dataclasses.field(
        default_factory=lambda: ["ec"],
    )
    dts_overlays: "list[str]" = dataclasses.field(default_factory=list)
    kconfig_files: "list[pathlib.Path]" = dataclasses.field(
        default_factory=list
    )
    project_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)
    inherited_from: typing.Iterable[str] = dataclasses.field(
        default_factory=list
    )
    signer: signers.BaseSigner = signers.NullSigner()

    @property
    def full_name(self) -> str:
        """Get the full project name, e.g. baseboard.variant"""
        inherited_from = (
            [self.inherited_from]
            if isinstance(self.inherited_from, str)
            else self.inherited_from
        )

        return ".".join([*inherited_from, self.project_name])


class Project:
    """An object encapsulating a project directory."""

    def __init__(self, config: ProjectConfig):
        self.config = config
        self.packer: zmake.output_packers.BasePacker = (
            self.config.output_packer(self)
        )
        self.signer = self.config.signer

    def iter_builds(self):
        """Iterate thru the build combinations provided by the project's packer.

        Yields:
            2-tuples of a build configuration name and a BuildConfig.
        """
        conf = build_config.BuildConfig(
            cmake_defs={"BOARD": self.config.zephyr_board}
        )

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
            dts_path = module_dts_overlay_name(
                module_path, self.config.zephyr_board
            )
            if dts_path.is_file():
                overlays.append(dts_path.resolve())

        for path in self.config.dts_overlays:
            # Support configs which don't explicitly put "here" in front.
            if isinstance(path, str):
                path = self.config.project_dir / path
            overlays.append(path.resolve())

        if overlays:
            return build_config.BuildConfig(
                cmake_defs={"DTC_OVERLAY_FILE": ";".join(map(str, overlays))}
            )
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
                    f"The {module!r} module is required by the "
                    f"{self.config.project_dir} project, but is not available."
                ) from e
        return result

    def get_toolchain(self, module_paths, override=None):
        """Get the first supported toolchain that is actually available."""
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
        for name in self.config.supported_toolchains:
            support_class = toolchains.support_classes[name]
            toolchain = support_class(name=name, modules=module_paths)
            if toolchain.probe():
                logging.info(
                    "Toolchain %r selected by probe function.", toolchain
                )
                return toolchain
        raise OSError(
            "No supported toolchains could be found on your system. If you see "
            "this message in the chroot, it indicates a bug. Otherwise, you'll "
            "either want to setup your system with a supported toolchain, or "
            "manually select an unsupported toolchain with the -t flag."
        )


@dataclasses.dataclass
class ProjectRegistrationHandler:
    """Return value of register_project.

    This is intended to be used to create simple variants of a project
    like so::

        brd = register_project(project_name="brd", ...)
        brd_changed = brd.variant(project_name="brd-changed", ...)
        brd_changed_again = brd_changed.variant(project_name="brd-changed-again", ...)
    """

    base_config: ProjectConfig
    register_func: typing.Callable[[], "ProjectRegistrationHandler"]

    def variant(self, **kwargs) -> "ProjectRegistrationHandler":
        """Register a new variant based on the base config.

        Args:
            kwargs: Any project config changes.  Note lists will be
                concatenated.

        Returns:
            Another ProjectRegistrationHandler.
        """
        new_config = dataclasses.asdict(self.base_config)
        new_config["inherited_from"] = [
            *self.base_config.inherited_from,
            self.base_config.project_name,
        ]

        for key, value in kwargs.items():
            if isinstance(value, list):
                new_config[key] = [*new_config[key], *value]
            else:
                new_config[key] = value
        return self.register_func(**new_config)


def load_config_file(path) -> typing.List[Project]:
    """Load a BUILD.py config file and create associated projects.

    Args:
        path: A pathlib.Path to the BUILD.py file.

    Returns:
        A list of Project objects specified by the file.
    """
    projects: typing.List[Project] = []

    def register_project(**kwargs) -> ProjectRegistrationHandler:
        config = ProjectConfig(**kwargs)
        projects.append(Project(config))
        return ProjectRegistrationHandler(
            base_config=config,
            register_func=register_project,
        )

    def assert_rw_fwid(project_name=None, addr=-1):
        for prj in projects:
            if prj.config.project_name == project_name:
                prj.packer.rw_fwid_addr = addr
                return
        raise KeyError(f"Project not found: {project_name} (in {path})")

    # The Python environment passed to the config file.
    config_globals = {
        "register_project": register_project,
        "assert_rw_fwid_DO_NOT_EDIT": assert_rw_fwid,
        "here": path.parent.resolve(),
    }

    # First, load the global helper functions.
    code = compile(
        pathlib.Path(configlib.__file__).read_bytes(),
        configlib.__file__,
        "exec",
    )
    exec(code, config_globals)  # pylint: disable=exec-used

    # Next, load the BUILD.py
    logging.debug("Loading config file %s", path)
    code = compile(path.read_bytes(), str(path), "exec")
    exec(code, config_globals)  # pylint: disable=exec-used
    logging.debug("Config file %s defines %s projects", path, len(projects))
    return projects


def find_projects(
    root_dirs: typing.List[typing.Union[str, os.PathLike]]
) -> typing.Dict[str, Project]:
    """Finds all zmake projects in root_dir.

    Args:
        root_dirs: an list of dirs as Pathlike objects

    Returns:
        A dictionary mapping project names to Project objects.
    """
    found_projects = {}
    for root_dir in root_dirs:
        if pathlib.Path(root_dir).exists():
            logging.debug("Finding zmake targets under '%s'.", root_dir)
            for path in pathlib.Path(root_dir).rglob("BUILD.py"):
                for project in load_config_file(path):
                    if project.config.project_name in found_projects:
                        raise KeyError(
                            f"Duplicate project defined: {project.config.project_name} (in {path})"
                        )
                    found_projects[project.config.project_name] = project
    return found_projects
