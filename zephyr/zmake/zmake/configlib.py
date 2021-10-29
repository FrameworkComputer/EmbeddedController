# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module defines helpers accessible to all BUILD.py files."""

import zmake.output_packers


def _register_project(name, **kwargs):
    kwargs.setdefault("project_dir", here)  # noqa: F821
    register_project(name, **kwargs)  # noqa: F821


def register_host_project(name, **kwargs):
    kwargs.setdefault("zephyr_board", "native_posix")
    kwargs.setdefault("supported_toolchains", ["llvm", "host"])
    kwargs.setdefault("output_packer", zmake.output_packers.ElfPacker)
    _register_project(name, **kwargs)


def register_host_test(name, **kwargs):
    kwargs.setdefault("is_test", True)
    register_host_project(name, **kwargs)


def register_raw_project(name, **kwargs):
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.RawBinPacker)
    _register_project(name, **kwargs)


def register_binman_project(name, **kwargs):
    kwargs.setdefault("output_packer", zmake.output_packers.BinmanPacker)
    register_raw_project(name, **kwargs)


def register_npcx_project(name, **kwargs):
    kwargs.setdefault("output_packer", zmake.output_packers.NpcxPacker)
    register_binman_project(name, **kwargs)
