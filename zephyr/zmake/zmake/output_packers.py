# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Types which provide many builds and composite them into a single binary."""

import logging
import os
from pathlib import Path
import shutil
import subprocess
from typing import Dict, Optional

from zmake import build_config
from zmake import util
import zmake.jobserver
import zmake.multiproc


class BasePacker:
    """Abstract base for all packers."""

    def __init__(self, project):
        self.project = project
        self.rw_fwid_addr = -1

    def configs(self):  # pylint: disable=no-self-use
        """Get all of the build configurations necessary.

        Yields:
            2-tuples of config name and a BuildConfig.
        """
        yield "singleimage", build_config.BuildConfig()

    def pack_firmware(
        self,
        work_dir,
        jobclient: zmake.jobserver.JobClient,
        dir_map: Dict[str, Path],
        version_string="",
    ):
        """Pack a firmware image.

        Config names from the configs generator are passed as keyword
        arguments, with each argument being set to the path of the
        build directory.

        Args:
            work_dir: A directory to write outputs and temporary files
            into.
            jobclient: A JobClient object to use.
            dir_map: A dict of build dirs such as {'ro': path_to_ro_dir}.
            version_string: The version string, which may end up in
               certain parts of the outputs.

        Yields:
            2-tuples of the path of each file in the work_dir (or any
            other directory) which should be copied into the output
            directory, and the output filename.
        """
        raise NotImplementedError("Abstract method not implemented")

    def verify_rw_fwid(
        self,
        _work_dir,
    ):
        """Verify the RW_FWID address matches what is expected.

        Args:
            _work_dir: A directory to write outputs and temporary files
            into.
        """
        # If the rw_fwid_addr is unset, then we don't need this implemented.
        # But if someone set rw_fwid_addr, the subclass must implement this
        # method.
        if self.rw_fwid_addr >= 0:
            raise NotImplementedError(
                f"{self}: Abstract method not implemented"
            )

    def _get_max_image_bytes(  # pylint: disable=no-self-use
        self, dir_map
    ) -> Optional[int]:
        """Get the maximum allowed image size (in bytes).

        This value will generally be found in CONFIG_FLASH_SIZE but may vary
        depending on the specific way things are being packed.

        Args:
            file: A file to test.
            dir_map: A dict of build dirs such as {'ro': path_to_ro_dir}.

        Returns:
            The maximum allowed size of the image in bytes, or None if the size
            is not limited.
        """
        del dir_map

    def _check_packed_file_size(self, file, dir_map):
        """Check that a packed file passes size constraints.

        Args:
            file: A file to test.
            dir_map: A dict of build dirs such as {'ro': path_to_ro_dir}.


        Returns:
            The file if it passes the test.
        """
        max_size = (  # pylint: disable=assignment-from-no-return
            self._get_max_image_bytes(dir_map)
        )
        if max_size is None or file.stat().st_size <= max_size:
            return file
        raise RuntimeError(f"Output file ({file}) too large")


class ElfPacker(BasePacker):
    """Raw proxy for ELF output of a single build."""

    def pack_firmware(self, work_dir, jobclient, dir_map, version_string=""):
        del version_string
        yield dir_map["singleimage"] / "zephyr" / "zephyr.elf", "zephyr.elf"


class RawBinPacker(BasePacker):
    """Raw proxy for zephyr.bin output of a single build."""

    def pack_firmware(self, work_dir, jobclient, dir_map, version_string=""):
        del version_string
        yield dir_map["singleimage"] / "zephyr" / "zephyr.bin", "ec.bin"


class IshBinPacker(BasePacker):
    """Raw proxy for ish_fw.bin output of a single build."""

    def pack_firmware(self, work_dir, jobclient, dir_map, version_string=""):
        del version_string
        single_dir = dir_map["singleimage"]
        yield single_dir / "zephyr" / "ish_fw.bin", "ish_fw.bin"
        yield (
            single_dir / "zephyr" / "component_manifest.json",
            "component_manifest.json",
        )


class BinmanPacker(BasePacker):
    """Packer for RO/RW image to generate a .bin build using FMAP."""

    ro_file = "zephyr.bin"
    rw_file = "zephyr.bin"

    def __init__(self, project):
        self.logger = logging.getLogger(self.__class__.__name__)
        super().__init__(project)

    def configs(self):
        yield "ro", build_config.BuildConfig(
            kconfig_defs={"CONFIG_CROS_EC_RO": "y"},
            cmake_defs={"CMAKE_C_FLAGS": "-DSECTION_IS_RO"},
        )
        yield "rw", build_config.BuildConfig(
            kconfig_defs={"CONFIG_CROS_EC_RW": "y"},
            cmake_defs={"CMAKE_C_FLAGS": "-DSECTION_IS_RW"},
        )

    def pack_firmware(
        self,
        work_dir,
        jobclient: zmake.jobserver.JobClient,
        dir_map,
        version_string="",
    ):
        """Pack RO and RW sections using Binman.

        Binman configuration is expected to be found in the RO build
        device-tree configuration.

        Args:
            work_dir: The directory used for packing.
            jobclient: The client used to run subprocesses.
            dir_map: A dict of build dirs such as {'ro': path_to_ro_dir}.
            version_string: The version string to use in FRID/FWID.

        Yields:
            2-tuples of the path of each file in the work_dir that
            should be copied into the output directory, and the output
            filename.
        """
        ro_dir = dir_map["ro"]
        rw_dir = dir_map["rw"]
        dts_file_path = ro_dir / "zephyr" / "zephyr.dts"

        # Copy the inputs into the work directory so that Binman can
        # find them under a hard-coded name.
        shutil.copy2(
            ro_dir / "zephyr" / self.ro_file, work_dir / "zephyr_ro.bin"
        )
        shutil.copy2(
            ro_dir / "zephyr" / "zephyr.elf", work_dir / "zephyr_ro.elf"
        )
        shutil.copy2(
            rw_dir / "zephyr" / self.rw_file, work_dir / "zephyr_rw.bin"
        )
        shutil.copy2(
            rw_dir / "zephyr" / "zephyr.elf", work_dir / "zephyr_rw.elf"
        )

        version_file_path = work_dir / "version.txt"
        # Version in FRID/FWID can be at most 31 bytes long (32, minus
        # one for null character).
        with open(version_file_path, "w", encoding="utf-8") as version_file:
            version_file.write(version_string[:31].ljust(32, "\0"))

        # Binman needs any C compiler to use as a C preprocessor.  Find one.
        for path in [
            os.environ.get("BUILD_CC"),
            os.environ.get("CC"),
            "/usr/bin/x86_64-pc-linux-gnu-clang",
            "clang",
            "gcc",
        ]:
            if path:
                cpp = shutil.which(path)
                if cpp:
                    break
        else:
            raise OSError(
                "Unable to find any C compiler for binman to use as a "
                "preprocessor.  Try setting BUILD_CC in the environment."
            )

        env = {
            "CC": cpp,
            "DTC": str(util.get_tool_path("dtc")),
        }

        proc = jobclient.popen(
            [
                util.get_tool_path("python3.8"),
                util.get_tool_path("binman"),
                "-v",
                "5",
                "build",
                "-d",
                dts_file_path,
                "-m",
                "-O",
                work_dir,
            ],
            cwd=work_dir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
        )

        zmake.multiproc.LogWriter.log_output(
            self.logger, logging.DEBUG, proc.stdout
        )
        zmake.multiproc.LogWriter.log_output(
            self.logger, logging.ERROR, proc.stderr
        )
        if proc.wait(timeout=60):
            raise OSError("Failed to run binman")

        yield work_dir / "ec.bin", "ec.bin"
        yield rw_dir / "zephyr" / ".config", "ec.config"
        yield ro_dir / "zephyr" / "zephyr.elf", "zephyr.ro.elf"
        yield ro_dir / "zephyr" / "zephyr.lst", "zephyr.ro.lst"
        yield rw_dir / "zephyr" / "zephyr.elf", "zephyr.rw.elf"
        yield rw_dir / "zephyr" / "zephyr.lst", "zephyr.rw.lst"
        yield (
            rw_dir / "zephyr" / "component_manifest.json",
            "component_manifest.json",
        )

        token_db_name = "database.bin"
        token_paths = [
            ro_dir / token_db_name,
            rw_dir / token_db_name,
        ]
        if os.path.exists(token_paths[0]):
            util.merge_token_databases(token_paths, work_dir / token_db_name)
            yield work_dir / token_db_name, token_db_name

    def verify_rw_fwid(
        self,
        work_dir,
    ):
        """Verify the RW_FWID address matches what is expected.

        Args:
            work_dir: A directory to write outputs and temporary files
            into.
        """
        actual_rw_fwid_addr = -1
        with open(work_dir / "image.map", encoding="utf-8") as image_map:
            for line in image_map:
                addr, _, _, name = line.split(None, 4)
                if name == "rw-fwid":
                    actual_rw_fwid_addr = int(addr, 16)
        if self.rw_fwid_addr != actual_rw_fwid_addr:
            if self.rw_fwid_addr < 0:
                raise RuntimeError(
                    "Missing RW_FWID assertion. Add one to BUILD.py:\n"
                    "assert_rw_fwid_DO_NOT_EDIT(project_name="
                    f'"{self.project.config.project_name}", '
                    f"addr={actual_rw_fwid_addr:#x})"
                )
            if actual_rw_fwid_addr < 0:
                raise RuntimeError(
                    "Unexpected RW_FWID assertion. Please remove "
                    "assert_rw_fwid_DO_NOT_EDIT from BUILD.py for project_name "
                    f"{self.project.config.project_name}"
                )
            raise RuntimeError(
                f"{self.project.config.project_name}: Incorrect RW_FWID, "
                f"expected {self.rw_fwid_addr:#x} got {actual_rw_fwid_addr:#x}"
            )


class NpcxPacker(BinmanPacker):
    """Packer for RO/RW image to generate a .bin build using FMAP.

    This expects that the build is setup to generate a
    zephyr.npcx.bin for the RO image, which should be packed using
    Nuvoton's loader format.
    """

    ro_file = "zephyr.npcx.bin"
    npcx_monitor = "npcx_monitor.bin"

    def _get_max_image_bytes(self, dir_map):
        ro_dir = dir_map["ro"]
        rw_dir = dir_map["rw"]
        ro_size = util.read_kconfig_autoconf_value(
            ro_dir / "zephyr" / "include" / "generated" / "zephyr",
            "CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES",
        )
        rw_size = util.read_kconfig_autoconf_value(
            rw_dir / "zephyr" / "include" / "generated" / "zephyr",
            "CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES",
        )
        return max(int(ro_size, 0), int(rw_size, 0))

    # This can probably be removed too and just rely on binman to
    # check the sizes... see the comment above.
    def pack_firmware(self, work_dir, jobclient, dir_map, version_string=""):
        ro_dir = dir_map["ro"]
        for path, output_file in super().pack_firmware(
            work_dir,
            jobclient,
            dir_map,
            version_string=version_string,
        ):
            if output_file == "ec.bin":
                yield (
                    self._check_packed_file_size(path, dir_map),
                    "ec.bin",
                )
            else:
                yield path, output_file

        # Include the NPCX monitor file as an output artifact.
        yield ro_dir / self.npcx_monitor, self.npcx_monitor


# MCHP all we do is set binman's ro file to zephyr.mchp.bin
class MchpPacker(BinmanPacker):
    """Packer for RO/RW image to generate a .bin build using FMAP.

    This expects that the build is setup to generate a
    zephyr.mchp.bin for the RO image, which should be packed using
    Microchip's loader format.
    """

    ro_file = "zephyr.mchp.bin"
    second_loader = "second_loader_fw.bin"

    def _get_max_image_bytes(self, dir_map):
        ro_dir = dir_map["ro"]
        rw_dir = dir_map["rw"]
        ro_size = util.read_kconfig_autoconf_value(
            ro_dir / "zephyr" / "include" / "generated" / "zephyr",
            "CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES",
        )
        rw_size = util.read_kconfig_autoconf_value(
            rw_dir / "zephyr" / "include" / "generated" / "zephyr",
            "CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES",
        )
        return max(int(ro_size, 0), int(rw_size, 0))

    def pack_firmware(self, work_dir, jobclient, dir_map, version_string=""):
        ro_dir = dir_map["ro"]
        for path, output_file in super().pack_firmware(
            work_dir,
            jobclient,
            dir_map,
            version_string=version_string,
        ):
            if output_file == "ec.bin":
                yield (
                    self._check_packed_file_size(path, dir_map),
                    "ec.bin",
                )
            else:
                yield path, output_file

        # Include the MCHP second loader file as an output artifact.
        yield ro_dir / self.second_loader, self.second_loader


# A dictionary mapping packer config names to classes.
packer_registry = {
    "binman": BinmanPacker,
    "elf": ElfPacker,
    "npcx": NpcxPacker,
    "raw": RawBinPacker,
    "mchp": MchpPacker,
}
