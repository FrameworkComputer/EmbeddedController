# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _impl(ctx):
    envs = {
        "COREBOOT_SDK_ROOT": ctx.file._coreboot_root.path,
        "TOOL_PATH_binman": ctx.file._binman_path.path,
    }

    build_dir = ctx.actions.declare_file("ec_{}".format(ctx.attr.name))

    args = [
        "--modules-dir",
        "external",
        "--zephyr-base",
        "external/zephyr",
        "build",
        ctx.attr.board,
        "-B",
        build_dir.path,
        "-t",
        "coreboot-sdk",
        "--extra-cflags='-DCMAKE_BUILD_TYPE=Debug'",
        # Use bazel's cache instead of CMake
        "-DUSE_CCACHE=0",
    ]

    deps = ctx.files._zmake + ctx.files._ec + ctx.files._binman_path + ctx.files._coreboot_root

    extra_modules = ctx.attr.extra_modules

    if "cmsis" in extra_modules:
        deps += ctx.files._cmsis

    zmake_bin_tool_inputs, zmake_bin_tool_input_mfs = ctx.resolve_tools(tools = [ctx.attr._zmake_bin])

    ctx.actions.run(
        outputs = [build_dir],
        inputs = deps,
        tools = zmake_bin_tool_inputs,
        executable = ctx.executable._zmake_bin,
        arguments = args,
        mnemonic = "RunBinary",
        use_default_shell_env = False,
        env = envs,
        input_manifests = zmake_bin_tool_input_mfs,
    )

    return DefaultInfo(
        files = depset([build_dir]),
        runfiles = ctx.runfiles(files = [build_dir]),
    )

ec_binary = rule(
    implementation = _impl,
    doc = "Build an EC binary for a given firmware target <name>",
    attrs = {
        "board": attr.string(),
        "extra_modules": attr.string_list(
            doc = "Extra modules, e.g. cmsis to be included for this ec binary",
        ),
        "_binman_path": attr.label(default = "@u_boot//:binman_path", allow_single_file = True),
        "_cmsis": attr.label(default = "@cmsis//:src", allow_files = True),
        "_coreboot_root": attr.label(default = "@coreboot_sdk//:coreboot_sdk_root", allow_single_file = True),
        "_ec": attr.label(default = "@ec//:src", allow_files = True),
        "_zmake": attr.label(default = "@zephyr//:src", allow_files = True),
        "_zmake_bin": attr.label(
            executable = True,
            allow_files = True,
            cfg = "exec",
            default = "@ec//:zmake",
        ),
    },
)
