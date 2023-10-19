#!/usr/bin/env vpython3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Check Zephyr RAM size.

This script enforces that Zephyr builds have enough free RAM to satisfy
CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE.
"""

# [VPYTHON:BEGIN]
# wheel: <
#   name: "infra/python/wheels/pyelftools-py2_py3"
#   version: "version:0.29"
# >
# [VPYTHON:END]

import argparse
from pathlib import Path
import sys

# pylint: disable=import-error
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


EXCLUDED_BOARDS = [
    # Only 116 bytes of free RAM (b/289320553)
    "gothrax",
    # Only 800 bytes of free RAM (b/289320515)
    "nereid",
]


class BuildInfo:
    """Extract relevant symbols from a Zephyr ELF file."""

    def __init__(self, elf_file: Path):
        self.syms = {}
        self.path = elf_file

        with open(elf_file, "rb") as file:
            elf = ELFFile(file)
            for section in elf.iter_sections():
                if isinstance(section, SymbolTableSection):
                    self.syms = {
                        s.name: s.entry.st_value for s in section.iter_symbols()
                    }
                    break

    def get_symbol(self, sym):
        """Get a symbol from the ELF or None if it doesn't exist."""
        return self.syms.get(sym)

    def get_remaining_ram(self):
        """Return the amount of free RAM in bytes"""

        used_ram_end = self.get_symbol("_image_ram_end")

        # How the RAM size is indicated varies by SOC
        if self.get_symbol("CONFIG_BOARD_NPCX9") or self.get_symbol(
            "CONFIG_BOARD_IT8XXX2"
        ):
            ram_base = self.get_symbol("CONFIG_SRAM_BASE_ADDRESS")
            assert (
                ram_base is not None
            ), "CONFIG_SRAM_BASE_ADDRESS is not defined"

            ram_size = self.get_symbol("CONFIG_SRAM_SIZE")
            assert ram_size is not None, "CONFIG_SRAM_SIZE is not defined"
            ram_size *= 1024  # Convert KiB to bytes

        else:
            ram_base = self.get_symbol("CONFIG_CROS_EC_RAM_BASE")
            assert (
                ram_base is not None
            ), "CONFIG_CROS_EC_RAM_BASE is not defined"

            ram_size = self.get_symbol("CONFIG_CROS_EC_RAM_SIZE")
            assert (
                ram_size is not None
            ), "CONFIG_CROS_EC_RAM_SIZE is not defined"

        return ram_base + ram_size - used_ram_end


def main(elf_path: Path):
    """Run the actual RAM space check."""
    build_info = BuildInfo(elf_path)

    # Exempt certain boards from test by checking if CONFIG_BOARD_{X} exists
    if any(
        {
            build_info.get_symbol(f"CONFIG_BOARD_{b.upper()}") is not None
            for b in EXCLUDED_BOARDS
        }
    ):
        return

    assert (
        build_info.get_symbol("_image_ram_end") is not None
    ), "The symbol _image_ram_end must be present in the ELF."

    min_preserved_ram = build_info.get_symbol(
        "CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE"
    )
    if min_preserved_ram:
        # Check that enough empty space was left at the end of RAM
        unused_ram = build_info.get_remaining_ram()
        assert unused_ram >= min_preserved_ram, (
            f"Insufficient free RAM remaining ({unused_ram} bytes) in "
            f"'{elf_path}'. Need at least "
            f"CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE ({min_preserved_ram} "
            f"bytes)"
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path, help="Path to ELF file to examine")
    parser.add_argument(
        "--delete", action="store_true", help="Delete failing ELF files"
    )

    args = parser.parse_args()

    try:
        main(args.elf)
    except:
        if args.delete:
            args.elf.unlink()
            sys.stderr.write(f"Deleted {args.elf}\n")
        raise
