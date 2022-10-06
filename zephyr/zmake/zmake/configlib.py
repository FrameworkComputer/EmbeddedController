# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module defines helpers accessible to all BUILD.py files."""

import zmake.output_packers


def _register_project(**kwargs):
    kwargs.setdefault(
        "project_dir", here  # noqa: F821 pylint: disable=undefined-variable
    )
    return register_project(
        **kwargs
    )  # noqa: F821 pylint: disable=undefined-variable


def register_host_project(**kwargs):
    """Register a project that runs on a posix host."""
    kwargs.setdefault("zephyr_board", "native_posix")
    kwargs.setdefault("supported_toolchains", ["llvm", "host"])
    kwargs.setdefault("output_packer", zmake.output_packers.ElfPacker)
    return _register_project(**kwargs)


def register_host_test(test_name, **kwargs):
    """Register a test project that runs on the host."""
    kwargs.setdefault("is_test", True)
    return register_host_project(
        project_name="test-{}".format(test_name), **kwargs
    )


def register_raw_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.RawBinPacker)
    return _register_project(**kwargs)


def register_binman_project(**kwargs):
    """Register a project that uses BinmanPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.BinmanPacker)
    return register_raw_project(**kwargs)


def register_npcx_project(**kwargs):
    """Register a project that uses NpcxPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.NpcxPacker)
    return register_binman_project(**kwargs)


def register_mchp_project(**kwargs):
    kwargs.setdefault("output_packer", zmake.output_packers.MchpPacker)
    return register_binman_project(**kwargs)
