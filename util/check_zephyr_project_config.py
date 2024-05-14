#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Validate Zephyr project configuration files."""

import logging
import os
import pathlib
import site
import sys
import tempfile

import preupload.lib


EC_BASE = pathlib.Path(__file__).parent.parent

if "ZEPHYR_BASE" in os.environ:
    ZEPHYR_BASE = pathlib.Path(os.environ.get("ZEPHYR_BASE"))
else:
    ZEPHYR_BASE = pathlib.Path(
        EC_BASE.resolve().parent.parent / "third_party" / "zephyr" / "main"
    )

if not os.path.exists(ZEPHYR_BASE):
    raise Exception(
        f"ZEPHYR_BASE path does not exist!\nZEPHYR_BASE={ZEPHYR_BASE}"
    )


site.addsitedir(ZEPHYR_BASE / "scripts")
site.addsitedir(ZEPHYR_BASE / "scripts" / "kconfig")
# pylint:disable=import-error,wrong-import-position
import kconfiglib
import zephyr_module


# pylint:enable=import-error,wrong-import-position

# Known configuration file extensions.
CONF_FILE_EXT = (".conf", ".overlay", "_defconfig")


def _parse_args(argv):
    parser = preupload.lib.argument_parser(description=__doc__)

    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Verbose Output"
    )
    parser.add_argument(
        "-d",
        "--dt-has",
        action="store_true",
        help="Check for options that depends on a DT_HAS_..._ENABLE symbol.",
    )

    args = parser.parse_args(argv)
    preupload.lib.populate_default_filenames(args)
    return args


def _init_log(verbose):
    """Initialize a logger object."""
    console = logging.StreamHandler()
    console.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))

    log = logging.getLogger(__file__)
    log.addHandler(console)

    if verbose:
        log.setLevel(logging.DEBUG)

    return log


def _default_y(defaults):
    """Return true if the symbol default is 'default y'

    True if the symbol has any 'default y' definition, regardless of other
    conditions.
    """
    for val, _ in defaults:
        if (
            isinstance(val, kconfiglib.Symbol)
            and val.is_constant
            and val.str_value == "y"
        ):
            return True
    return False


def _default_y_if_ztest(defaults):
    """Return true if the symbol default is 'default y' if ZTEST


    True if the symbol has any 'default y if ZTEST' definition.
    """
    for val, cond in defaults:
        if (
            isinstance(val, kconfiglib.Symbol)
            and val.is_constant
            and val.str_value == "y"
            and isinstance(cond, kconfiglib.Symbol)
            and cond.name == "ZTEST"
        ):
            return True
    return False


class KconfigCheck:
    """Validate Zephyr project configuration files.

    Attributes:
        verbose: whether to enable verbose mode logging
    """

    def __init__(self, verbose):
        self.log = _init_log(verbose)
        self.fail_count = 0

        # Preload the upstream Kconfig.
        self.program_kconf = {None: self._init_kconfig(None)}

    def _init_kconfig(self, filename):
        """Initialize a kconfiglib object with all boards and arch options.

        Args:
            filename: the path of the Kconfig file to load.

        Returns:
            A kconfiglib.Kconfig object.
        """
        with tempfile.TemporaryDirectory() as temp_dir:
            modules = zephyr_module.parse_modules(
                ZEPHYR_BASE, modules=[EC_BASE]
            )

            kconfig = ""
            for module in modules:
                kconfig += zephyr_module.process_kconfig(
                    module.project, module.meta
                )

            # generate Kconfig.modules file
            with open(
                pathlib.Path(temp_dir) / "Kconfig.modules",
                "w",
                encoding="utf-8",
            ) as file:
                file.write(kconfig)

            # generate few more stub files
            (pathlib.Path(temp_dir) / "Kconfig.dts").touch()
            (pathlib.Path(temp_dir) / "soc").mkdir()
            (pathlib.Path(temp_dir) / "soc" / "Kconfig.soc").touch()
            (pathlib.Path(temp_dir) / "soc" / "Kconfig.defconfig").touch()
            (pathlib.Path(temp_dir) / "arch").mkdir()
            (pathlib.Path(temp_dir) / "arch" / "Kconfig").touch()

            os.environ["ZEPHYR_BASE"] = str(ZEPHYR_BASE)
            os.environ["srctree"] = str(ZEPHYR_BASE)
            os.environ["KCONFIG_BINARY_DIR"] = temp_dir
            os.environ["ARCH_DIR"] = "arch"
            os.environ["ARCH"] = "*"
            os.environ["HWM_SCHEME"] = "v2"
            os.environ["BOARD_DIR"] = "boards/posix/native_posix"

            if not filename:
                filename = os.path.join(ZEPHYR_BASE, "Kconfig")

            self.log.info("Loading Kconfig: %s", filename)

            return kconfiglib.Kconfig(filename)

    def _kconf_from_path(self, path):
        """Return a Kconfig object for the specified path.

        If path resides under zephyr/program, find the name of the program and
        look for a corresponding program specific Kconfig file. If one is
        present, return a corresponding Kconfig object for the program.

        Stores a list of per-program Kconfig objects internally, so each
        program Kconfig is only loaded once.

        Args:
            path: the path of the Kconfig file to load.

        Returns:
            A kconfiglib.Kconfig object.
        """
        program_path = pathlib.Path(EC_BASE, "zephyr", "program")
        file_path = pathlib.Path(path).resolve()

        program = None
        program_kconfig = None
        if program_path in file_path.parents:
            idx = file_path.parents.index(program_path)
            program = file_path.parts[-(idx + 1)]
            kconfig_path = pathlib.Path(program_path, program, "Kconfig")
            if kconfig_path.is_file():
                program_kconfig = kconfig_path

        self.log.info(
            "Path: %s, program: %s, program_kconfig: %s",
            path,
            program,
            program_kconfig,
        )

        if program not in self.program_kconf:
            if not program_kconfig:
                self.program_kconf[program] = self.program_kconf[None]
            else:
                self.program_kconf[program] = self._init_kconfig(
                    program_kconfig
                )

        return self.program_kconf[program]

    def _fail(self, *args):
        """Report a fail in the error log and increment the fail counter."""
        self.fail_count += 1
        self.log.error(*args)

    def _filter_config_files(self, files):
        """Yield files with known config suffixes from the command line."""
        for file in files:
            if not file.exists():
                self.log.info("Ignoring %s: file has been removed", file)
                continue

            if not file.name.endswith(CONF_FILE_EXT):
                self.log.info("Ignoring %s: unrecognized suffix", file)
                continue

            yield file

    def _check_dt_has(self, file_name):
        """Check file_name for known automatic config options.

        Check file_name for any explicitly enabled option that has a dependency
        on a devicetree symbol. These are normally enabled automatically so
        there's no point enabling them explicitly.
        """
        kconf = self._kconf_from_path(file_name)

        symbols = {}
        for name, val in kconf.syms.items():
            dep = kconfiglib.expr_str(val.direct_dep)
            if "DT_HAS_" in dep:
                if _default_y(val.orig_defaults) and not _default_y_if_ztest(
                    val.orig_defaults
                ):
                    symbols[name] = dep

        self.log.info("Checking %s", file_name)

        with open(file_name, "r", encoding="utf-8") as file:
            for line_num, line in enumerate(file.readlines(), start=1):
                for name, dep in symbols.items():
                    match = f"CONFIG_{name}=y"
                    if line.startswith(match):
                        self._fail(
                            "%s:%d: unnecessary config option %s (depends on %s)",
                            file_name,
                            line_num,
                            match,
                            dep,
                        )

    def run_checks(self, files, dt_has):
        """Run all config checks."""
        config_files = self._filter_config_files(files)

        for file in config_files:
            if dt_has:
                self._check_dt_has(file)

        return self.fail_count


def main(argv):
    """Main function"""
    args = _parse_args(argv)

    kconfig_checker = KconfigCheck(args.verbose)

    return kconfig_checker.run_checks(args.filename, args.dt_has)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
