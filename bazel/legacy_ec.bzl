# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _board_srcs(board, chip, core, real_board = None, baseboard = None):
    """Collect sources for a board.

    Args:
        board: The board name.
        chip: The chip name.
        core: The core name.
        real_board: If specified, the underlying directory from a symlinked
             board directory.
        baseboard: If specified, the baseboard directory to include.

    Returns:
        The label for the filegroup.
    """
    globs = [
        "chip/%s/**" % chip,
        "core/%s/**" % core,
    ]
    if real_board:
        globs += [
            "board/%s" % board,
            "board/%s/**" % real_board,
        ]
    else:
        globs.append("board/%s/**" % board)

    if baseboard:
        globs.append("baseboard/%s/**" % baseboard)

    if chip == "npcx":
        globs.append("util/ecst.*")

    name = "legacy_ec_board_srcs_%s" % board
    native.filegroup(
        name = name,
        srcs = native.glob(globs),
    )
    return "//platform/ec:%s" % name

def _ec_srcs():
    """Collect common (non-board-specific) sources."""
    native.filegroup(
        name = "legacy_ec_makefile",
        srcs = ["Makefile"],
    )
    native.filegroup(
        name = "legacy_ec_srcs",
        srcs = native.glob([
            "Makefile.*",
            "builtin/**",
            "common/**",
            "core/build.mk",
            "crypto/build.mk",
            "driver/**",
            "fuzz/**",
            "include/**",
            "libc/build.mk",
            "power/**",
            "test/**",
            "third_party/**",
            "util/build.mk",
            "util/genvif.*",
            "util/getversion.sh",
            "util/lock/**",
        ]),
    )

def _impl(ctx):
    build_dir = ctx.actions.declare_file("ec_%s" % ctx.attr.name)

    env = {
        "BOARD": ctx.attr.board,
        "BUILD_DIR": build_dir.path,
        "COREBOOT_SDK_ROOT": ctx.file._coreboot_root.path,
        "EC_DIR": ctx.file._legacy_ec_makefile.dirname,
    }

    deps = (ctx.files._legacy_ec_makefile +
            ctx.files._legacy_ec_srcs +
            ctx.files.board_srcs +
            ctx.files._coreboot_root)

    # Have to use shell as we need to realpath pretty much everything
    # (need to change directories for the Makefile).
    commands = [
        "set -x",
        'mkdir "${BUILD_DIR}"',
    ]
    make_args = [
        "BOARD=${BOARD}",
        "CCACHE=",
        'CROSS_COMPILE_arm=$(realpath "${COREBOOT_SDK_ROOT}")/bin/arm-eabi-',
        'CROSS_COMPILE_nds32=$(realpath "${COREBOOT_SDK_ROOT}")/bin/nds32le-elf-',
        'CROSS_COMPILE_riscv=$(realpath "${COREBOOT_SDK_ROOT}")/bin/riscv64-elf-',
        'CROSS_COMPILE_x86=$(realpath "${COREBOOT_SDK_ROOT}")/bin/i386-elf-',
        'out=$(realpath "${BUILD_DIR}")',
        "SHELL=/bin/bash",
        "HOSTCC=/usr/bin/clang -Wno-unknown-warning-option",
        "BUILDCC=/usr/bin/clang -Wno-unknown-warning-option",
        '$(realpath "${BUILD_DIR}")/ec.bin',
    ]
    commands.append("MAKE_ARGS=(%s)" % " ".join(['"%s"' % x for x in make_args]))
    commands.append('cd "${EC_DIR}" && make "${MAKE_ARGS[@]}"')
    command = ";".join(commands)

    ctx.actions.run_shell(
        outputs = [build_dir],
        inputs = deps,
        command = command,
        env = env,
        mnemonic = "BuildLegacyEC",
    )

    return DefaultInfo(
        files = depset([build_dir]),
        runfiles = ctx.runfiles(files = [build_dir]),
    )

_rule = rule(
    implementation = _impl,
    doc = "Build an EC binary for a given legacy EC firmware target <name>",
    attrs = {
        "board": attr.string(),
        "board_srcs": attr.label(allow_files = True),
        "_coreboot_root": attr.label(
            default = "@coreboot_sdk//:coreboot_sdk_root",
            allow_single_file = True,
        ),
        "_legacy_ec_makefile": attr.label(
            default = "//platform/ec:legacy_ec_makefile",
            allow_single_file = True,
        ),
        "_legacy_ec_srcs": attr.label(
            default = "//platform/ec:legacy_ec_srcs",
            allow_files = True,
        ),
    },
)

legacy_ec = struct(
    board_srcs = _board_srcs,
    ec_srcs = _ec_srcs,
    rule = _rule,
)
