# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Types which provide many builds and composite them into a single binary."""
import logging
import pathlib
import subprocess

import zmake.build_config as build_config
import zmake.multiproc
import zmake.util as util


def _write_dts_file(dts_file, config_header, output_bin, ro_filename, rw_filename):
    """Generate the .dts file used for binman.

    Args:
        dts_file: The dts file to write to.
        config_header: The full path to the generated autoconf.h header.
        output_bin: The full path to the binary that binman should output.
        ro_filename: The RO image file name.
        rw_filename: The RW image file name.

    Returns:
        The path to the .dts file that was generated.
    """
    dts_file.write("""
    /dts-v1/;
    #include "{config_header}"
    / {{
      #address-cells = <1>;
      #size-cells = <1>;
      binman {{
        filename = "{output_bin}";
        pad-byte = <0x1d>;
        section@0 {{
          read-only;
          offset = <CONFIG_CROS_EC_RO_MEM_OFF>;
          size = <CONFIG_CROS_EC_RO_SIZE>;
          blob {{
            filename = "{ro_filename}";
          }};
        }};
        section@1 {{
          offset = <CONFIG_CROS_EC_RW_MEM_OFF>;
          size = <CONFIG_CROS_EC_RW_SIZE>;
          blob {{
            filename = "{rw_filename}";
          }};
        }};
      }};
    }};""".format(
        output_bin=output_bin,
        config_header=config_header,
        ro_filename=ro_filename,
        rw_filename=rw_filename
    ))


class BasePacker:
    """Abstract base for all packers."""
    def __init__(self, project):
        self.project = project

    def configs(self):
        """Get all of the build configurations necessary.

        Yields:
            2-tuples of config name and a BuildConfig.
        """
        yield 'singleimage', build_config.BuildConfig()

    def pack_firmware(self, work_dir, jobclient):
        """Pack a firmware image.

        Config names from the configs generator are passed as keyword
        arguments, with each argument being set to the path of the
        build directory.

        Args:
            work_dir: A directory to write outputs and temporary files
            into.
            jobclient: A JobClient object to use.

        Yields:
            2-tuples of the path of each file in the work_dir (or any
            other directory) which should be copied into the output
            directory, and the output filename.
        """
        raise NotImplementedError('Abstract method not implemented')


class ElfPacker(BasePacker):
    """Raw proxy for ELF output of a single build."""
    def pack_firmware(self, work_dir, jobclient, singleimage):
        yield singleimage / 'zephyr' / 'zephyr.elf', 'zephyr.elf'


class RawBinPacker(BasePacker):
    """Packer for RO/RW image to generate a .bin build using FMAP."""
    def __init__(self, project):
        self.logger = logging.getLogger(self.__class__.__name__)
        super().__init__(project)

    def configs(self):
        yield 'ro', build_config.BuildConfig(kconfig_defs={'CONFIG_CROS_EC_RO': 'y'})
        yield 'rw', build_config.BuildConfig(kconfig_defs={'CONFIG_CROS_EC_RW': 'y'})

    def pack_firmware(self, work_dir, jobclient, ro, rw):
        """Pack the 'raw' binary.

        This combines the RO and RW images as specified in the Kconfig file for
        the project. For this function to work, the following config values must
        be defined:
        * CONFIG_CROS_EC_RO_MEM_OFF - The offset in bytes of the RO image from
          the start of the resulting binary.
        * CONFIG_CROS_EC_RO_SIZE - The maximum allowed size (in bytes) of the RO
          image.
        * CONFIG_CROS_EC_RW_MEM_OFF - The offset in bytes of the RW image from
          the start of the resulting binary (must be >= RO_MEM_OFF + RO_SIZE).
        * CONFIG_CROS_EC_RW_SIZE - The maximum allowed size (in bytes) of the RW
           image.

        Args:
            work_dir: The directory used for packing.
            jobclient: The client used to run subprocesses.
            ro: Directory containing the RO image build.
            rw: Directory containing the RW image build.

        Returns:
            Tuple mapping the resulting .bin file to the output filename.
        """
        work_dir = pathlib.Path(work_dir).resolve()
        ro = pathlib.Path(ro).resolve()
        rw = pathlib.Path(rw).resolve()
        dts_file_path = work_dir / 'project.dts'
        with open(dts_file_path, 'w+') as dts_file:
            _write_dts_file(
                dts_file=dts_file,
                config_header=ro / 'zephyr' / 'include' / 'generated' / 'autoconf.h',
                output_bin=work_dir / 'zephyr.bin',
                ro_filename=ro / 'zephyr' / 'zephyr.bin',
                rw_filename=rw / 'zephyr' / 'zephyr.bin')

        proc = jobclient.popen(
            ['binman', '-v', '5', 'build', '-d', dts_file_path, '-m'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding='utf-8')

        zmake.multiproc.log_output(self.logger, logging.DEBUG, proc.stdout)
        zmake.multiproc.log_output(self.logger, logging.ERROR, proc.stderr)
        if proc.wait(timeout=5):
            raise OSError('Failed to run binman')

        yield work_dir / 'zephyr.bin', 'zephyr.bin'


# A dictionary mapping packer config names to classes.
packer_registry = {
    'elf': ElfPacker,
    'raw': RawBinPacker,
}
