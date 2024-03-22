# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module defines helpers accessible to all BUILD.py files."""

from zmake import signers  # pylint: disable=unused-import
import zmake.output_packers


def _register_project(**kwargs):
    kwargs.setdefault(
        "project_dir", here  # noqa: F821 pylint: disable=undefined-variable
    )
    return register_project(  # noqa: F821 pylint: disable=undefined-variable
        **kwargs
    )


#
# Note - the "ec" module must always be last in the list of modules.
# This permits the EC code to access any public APIs from the included modules.
#


def register_host_project(**kwargs):
    """Register a project that runs on a posix host."""
    kwargs.setdefault("zephyr_board", "native_posix")
    kwargs.setdefault("supported_toolchains", ["llvm", "host"])
    kwargs.setdefault("output_packer", zmake.output_packers.ElfPacker)
    return _register_project(**kwargs)


def register_raw_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.RawBinPacker)
    return _register_project(**kwargs)


# TODO: b/303828221 - Validate tokenizing with ITE
def register_binman_project(**kwargs):
    """Register a project that uses BinmanPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.BinmanPacker)
    return register_raw_project(**kwargs)


def register_npcx_project(**kwargs):
    """Register a project that uses NpcxPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.NpcxPacker)
    kwargs.setdefault("modules", ["cmsis", "ec"])
    return register_binman_project(**kwargs)


def register_mchp_project(**kwargs):
    """Register a project that uses MchpPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.MchpPacker)
    kwargs.setdefault("modules", ["cmsis", "ec"])
    return register_binman_project(**kwargs)


def register_ish_project(**kwargs):
    """Register a project that uses IshBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.IshBinPacker)
    kwargs.setdefault("modules", ["ec", "cmsis", "hal_intel_public"])
    return _register_project(**kwargs)
