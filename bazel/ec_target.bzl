# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "@cros_firmware//platform/ec/bazel:zephyr_ec.bzl",
    "ec_binary",
)
load(
    "@cros_firmware//platform/ec/bazel:legacy_ec.bzl",
    "legacy_ec",
)
load(
    "@cros_firmware//platform/ec/bazel:flash_ec.bzl",
    "flash_ec",
)

def ec_target(
        name,
        extra_modules = [],
        zephyr = True,
        real_board = None,
        baseboard = None,
        chip = None,
        core = None):
    """Create build rules for an EC target.

    Args:
        name: The board name.
        extra_modules: For Zephyr boards only, the required modules
            (besides EC).
        zephyr: Is this board a Zephyr board?
        real_board: For Legacy EC boards, if the board is just a symlink, the
            name of the real underlying board directory the symlink points to.
        baseboard: For Legacy EC boards, the baseboard directory to include.
        chip: For Legacy EC boards, the chip directory to include.
        core: For Legacy EC boards, the board directory to include.
    """
    if zephyr:
        ec_binary(name = name, extra_modules = extra_modules)
    else:
        board_srcs = legacy_ec.board_srcs(
            board = name,
            real_board = real_board,
            baseboard = baseboard,
            chip = chip,
            core = core,
        )
        legacy_ec.rule(name = name, board_srcs = board_srcs)
    flash_ec(
        name = "flash_ec_{}".format(name),
        board = name,
        build_target = "@cros_firmware//platform/ec:{}".format(name),
    )
