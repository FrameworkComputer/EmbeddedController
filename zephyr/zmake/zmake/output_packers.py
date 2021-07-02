# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Types which provide many builds and composite them into a single binary."""
import logging
import shutil
import subprocess

import zmake.build_config as build_config
import zmake.multiproc
import zmake.util as util


class BasePacker:
    """Abstract base for all packers."""

    def __init__(self, project):
        self.project = project

    def configs(self):
        """Get all of the build configurations necessary.

        Yields:
            2-tuples of config name and a BuildConfig.
        """
        yield "singleimage", build_config.BuildConfig()

    def pack_firmware(self, work_dir, jobclient, version_string=""):
        """Pack a firmware image.

        Config names from the configs generator are passed as keyword
        arguments, with each argument being set to the path of the
        build directory.

        Args:
            work_dir: A directory to write outputs and temporary files
            into.
            jobclient: A JobClient object to use.
            version_string: The version string, which may end up in
               certain parts of the outputs.

        Yields:
            2-tuples of the path of each file in the work_dir (or any
            other directory) which should be copied into the output
            directory, and the output filename.
        """
        raise NotImplementedError("Abstract method not implemented")

    def _get_max_image_bytes(self):
        """Get the maximum allowed image size (in bytes).

        This value will generally be found in CONFIG_FLASH_SIZE but may vary
        depending on the specific way things are being packed.

        Returns:
            The maximum allowed size of the image in bytes.
        """
        raise NotImplementedError("Abstract method not implemented")

    def _is_size_bound(self, path):
        """Check whether the given path should be constrained by size.

        Generally, .elf files will be unconstrained while .bin files will be
        constrained.

        Args:
            path: A file's path to test.

        Returns:
            True if the file size should be checked. False otherwise.
        """
        return path.suffix == ".bin"

    def _check_packed_file_size(self, file, dirs):
        """Check that a packed file passes size constraints.

        Args:
            file: A file to test.
            dirs: A map of the arguments to pass to _get_max_image_bytes

        Returns:
            The file if it passes the test.
        """
        if not self._is_size_bound(
            file
        ) or file.stat().st_size <= self._get_max_image_bytes(**dirs):
            return file
        raise RuntimeError("Output file ({}) too large".format(file))


class ElfPacker(BasePacker):
    """Raw proxy for ELF output of a single build."""

    def pack_firmware(self, work_dir, jobclient, singleimage, version_string=""):
        yield singleimage / "zephyr" / "zephyr.elf", "zephyr.elf"


class RawBinPacker(BasePacker):
    """Raw proxy for zephyr.bin output of a single build."""

    def pack_firmware(self, work_dir, jobclient, singleimage, version_string=""):
        yield singleimage / "zephyr" / "zephyr.bin", "zephyr.bin"


class BinmanPacker(BasePacker):
    """Packer for RO/RW image to generate a .bin build using FMAP."""

    ro_file = "zephyr.bin"
    rw_file = "zephyr.bin"

    def __init__(self, project):
        self.logger = logging.getLogger(self.__class__.__name__)
        super().__init__(project)

    def configs(self):
        yield "ro", build_config.BuildConfig(kconfig_defs={"CONFIG_CROS_EC_RO": "y"})
        yield "rw", build_config.BuildConfig(kconfig_defs={"CONFIG_CROS_EC_RW": "y"})

    def pack_firmware(self, work_dir, jobclient, ro, rw, version_string=""):
        """Pack RO and RW sections using Binman.

        Binman configuration is expected to be found in the RO build
        device-tree configuration.

        Args:
            work_dir: The directory used for packing.
            jobclient: The client used to run subprocesses.
            ro: Directory containing the RO image build.
            rw: Directory containing the RW image build.
            version_string: The version string to use in FRID/FWID.

        Yields:
            2-tuples of the path of each file in the work_dir that
            should be copied into the output directory, and the output
            filename.
        """
        dts_file_path = ro / "zephyr" / "zephyr.dts"

        # Copy the inputs into the work directory so that Binman can
        # find them under a hard-coded name.
        shutil.copy2(ro / "zephyr" / self.ro_file, work_dir / "zephyr_ro.bin")
        shutil.copy2(rw / "zephyr" / self.rw_file, work_dir / "zephyr_rw.bin")

        # Version in FRID/FWID can be at most 31 bytes long (32, minus
        # one for null character).
        if len(version_string) > 31:
            version_string = version_string[:31]

        proc = jobclient.popen(
            [
                "binman",
                "-v",
                "5",
                "build",
                "-a",
                "version={}".format(version_string),
                "-d",
                dts_file_path,
                "-m",
                "-O",
                work_dir,
            ],
            cwd=work_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
        )

        zmake.multiproc.log_output(self.logger, logging.DEBUG, proc.stdout)
        zmake.multiproc.log_output(self.logger, logging.ERROR, proc.stderr)
        if proc.wait(timeout=60):
            raise OSError("Failed to run binman")

        yield work_dir / "zephyr.bin", "zephyr.bin"
        yield ro / "zephyr" / "zephyr.elf", "zephyr.ro.elf"
        yield rw / "zephyr" / "zephyr.elf", "zephyr.rw.elf"


class NpcxPacker(BinmanPacker):
    """Packer for RO/RW image to generate a .bin build using FMAP.

    This expects that the build is setup to generate a
    zephyr.npcx.bin for the RO image, which should be packed using
    Nuvoton's loader format.
    """

    ro_file = "zephyr.npcx.bin"

    # TODO(b/192401039): CONFIG_FLASH_SIZE is nuvoton-only.  Since
    # binman already checks sizes, perhaps we can just remove this
    # code?
    def _get_max_image_bytes(self, ro, rw):
        ro_size = util.read_kconfig_autoconf_value(
            ro / "zephyr" / "include" / "generated", "CONFIG_FLASH_SIZE"
        )
        rw_size = util.read_kconfig_autoconf_value(
            ro / "zephyr" / "include" / "generated", "CONFIG_FLASH_SIZE"
        )
        return max(int(ro_size, 0), int(rw_size, 0)) * 1024

    # This can probably be removed too and just rely on binman to
    # check the sizes... see the comment above.
    def pack_firmware(self, work_dir, jobclient, ro, rw, version_string=""):
        for path, output_file in super().pack_firmware(
            work_dir,
            jobclient,
            ro,
            rw,
            version_string=version_string,
        ):
            if output_file == "zephyr.bin":
                yield (
                    self._check_packed_file_size(path, {"ro": ro, "rw": rw}),
                    "zephyr.bin",
                )
            else:
                yield path, output_file


# A dictionary mapping packer config names to classes.
packer_registry = {
    "binman": BinmanPacker,
    "elf": ElfPacker,
    "npcx": NpcxPacker,
    "raw": RawBinPacker,
}
