# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def _impl(ctx):
    args = ctx.attr._required_args + ctx.attr.args

    if "--outdir" in args:
        fail("--outdir is not permitted when run with Bazel")

    build_dir = ctx.actions.declare_file("twister-out")
    env = {
        # TODO(https://github.com/zephyrproject-rtos/zephyr/issues/59453):
        # This ought to be passed as a CMake variable but can't due to how
        # Zephyr calls verify-toolchain.cmake
        "ZEPHYR_TOOLCHAIN_VARIANT": "llvm",
        "TOOLCHAIN_ROOT": ctx.file._TOOLCHAIN_ROOT.path,
        # TODO(b/286589977): Remove this when hermetic clang is added
        "PATH": ":/bin:/usr/bin",
        "BAZEL_TWISTER_CWD": ctx.attr.cwd,
        "BAZEL_TWISTER_OUTDIR": build_dir.path,
    }

    deps = ctx.files._ec + ctx.files._zephyr

    twister_bin_tool_inputs, twister_bin_tool_input_mfs = \
        ctx.resolve_tools(tools = [ctx.attr._twister_bin])

    ctx.actions.run(
        outputs = [build_dir],
        inputs = deps,
        tools = twister_bin_tool_inputs,
        executable = ctx.executable._twister_bin,
        arguments = args,
        mnemonic = "twister",
        use_default_shell_env = False,
        env = env,
        input_manifests = twister_bin_tool_input_mfs,
    )

    return DefaultInfo(
        files = depset([build_dir]),
        runfiles = ctx.runfiles(files = [build_dir]),
    )

twister_test_binary = rule(
    implementation = _impl,
    doc = "Run a salty delicious pretzel. Also verify the EC code",
    attrs = {
        "args": attr.string_list(default = [
        ]),
        "cwd": attr.string(default = ""),
        "_ec": attr.label(default = "@ec//:src", allow_files = True),
        "_TOOLCHAIN_ROOT": attr.label(default = "@ec//:zephyr", allow_single_file = True),
        "_zephyr": attr.label(default = "@zephyr//:src", allow_files = True),
        "_required_args": attr.string_list(default = [
            "-x=USE_CCACHE=0",
            "-x=USER_CACHE_DIR=/tmp/twister_cache",
        ]),
        "_twister_bin": attr.label(
            executable = True,
            allow_files = True,
            cfg = "exec",
            default = "@ec//:twister_binary",
        ),
    },
)
