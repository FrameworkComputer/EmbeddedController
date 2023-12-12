# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_skylib//lib:shell.bzl", "shell")

def _gen_shell_wrapper(argv, env):
    script = "#!/bin/bash\n"
    for key, val in env.items():
        script += "export %s=%s\n" % (key, shell.quote(val))
    script += '%s "$@"\n' % " ".join([shell.quote(x) for x in argv])
    return script

def _flash_ec(ctx):
    env = {
        "BAZEL_DUT_CONTROL": ctx.executable._dut_control.short_path,
        "PATH": "%s/usr/bin:/usr/bin:/bin" % ctx.files._ec_devutils[0].path,
        "SHFLAGS": "%s/usr/share/misc/shflags" % ctx.files._shflags[0].path,
    }

    script = ctx.actions.declare_file("flash_ec_wrapper.sh")
    argv = [
        ctx.file._flash_ec.path,
        "--board",
        ctx.attr.board,
    ]

    if ctx.attr.zephyr:
        argv.append("--zephyr")
        image_path = "%s/%s/output/ec.bin" % (
            ctx.file.build_target.short_path,
            ctx.attr.board,
        )
    else:
        image_path = "%s/ec.bin" % ctx.file.build_target.short_path

    argv.extend(["--image", image_path])

    script_content = _gen_shell_wrapper(argv = argv, env = env)
    ctx.actions.write(script, script_content, is_executable = True)

    runfiles = ctx.runfiles(
        files = (
            ctx.files._flash_ec +
            ctx.files._ec_devutils +
            ctx.files._shflags +
            ctx.files.build_target
        ),
    )
    runfiles = runfiles.merge(ctx.attr._dut_control.default_runfiles)
    return [
        DefaultInfo(executable = script, runfiles = runfiles),
    ]

flash_ec = rule(
    implementation = _flash_ec,
    attrs = {
        "board": attr.string(doc = "Board name to flash."),
        "build_target": attr.label(
            doc = "Build target for this board.",
            allow_single_file = True,
        ),
        "zephyr": attr.bool(doc = "True if it's a Zephyr board."),
        "_dut_control": attr.label(
            doc = "The dut_control binary to use.",
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@hdctools//:dut_control"),
        ),
        "_ec_devutils": attr.label(
            doc = "The devutils bundle path.",
            allow_single_file = True,
            default = Label("@ec_devutils//:bundle"),
        ),
        "_flash_ec": attr.label(
            doc = "The flash_ec script to run.",
            allow_single_file = True,
            default = Label("@cros_firmware//platform/ec:util/flash_ec"),
        ),
        "_shflags": attr.label(
            doc = "The shflags bundle path.",
            allow_files = True,
            default = Label("@shflags//:bundle"),
        ),
    },
    executable = True,
)
