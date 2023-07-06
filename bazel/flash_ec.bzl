# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_skylib//lib:shell.bzl", "shell")

def _gen_shell_wrapper(argv, env):
    script = "#!/bin/bash\n"
    for key, val in env.items():
        script += "export %s=%s\n" % (key, shell.quote(val))
    script += "%s\n" % " ".join([shell.quote(x) for x in argv])
    return script

def _flash_ec(ctx):
    env = {
        "PATH": "%s/usr/bin:/usr/bin:/bin" % ctx.files.ec_devutils[0].path,
        "SHFLAGS": "%s/usr/share/misc/shflags" % ctx.files.shflags[0].path,
        "BAZEL_DUT_CONTROL": ctx.executable.dut_control.short_path,
    }

    script = ctx.actions.declare_file("flash_ec_wrapper.sh")
    script_content = _gen_shell_wrapper(
        argv = [
            ctx.files.flash_ec[0].path,
            "--board",
            ctx.attr.board,
            "--zephyr",
            "--image",
            "%s/%s/output/ec.bin" % (
                ctx.files.build_target[0].short_path,
                ctx.attr.board,
            ),
        ],
        env = env,
    )
    ctx.actions.write(script, script_content, is_executable = True)

    runfiles = ctx.runfiles(
        files = (
            ctx.files.flash_ec +
            ctx.files.ec_devutils +
            ctx.files.shflags +
            ctx.files.build_target
        ),
    )
    runfiles = runfiles.merge(ctx.attr.dut_control.default_runfiles)
    return [
        DefaultInfo(executable = script, runfiles = runfiles),
    ]

flash_ec = rule(
    implementation = _flash_ec,
    attrs = {
        "board": attr.string(doc = "Board name to flash."),
        "build_target": attr.label(
            doc = "Build target for this board.",
            allow_files = True,
        ),
        "flash_ec": attr.label(
            doc = "The flash_ec script to run.",
            allow_files = True,
            default = Label("@cros_firmware//platform/ec:util/flash_ec"),
        ),
        "ec_devutils": attr.label(
            doc = "The devutils bundle path.",
            allow_files = True,
            default = Label("@ec_devutils//:bundle"),
        ),
        "shflags": attr.label(
            doc = "The shflags bundle path.",
            allow_files = True,
            default = Label("@shflags//:bundle"),
        ),
        "dut_control": attr.label(
            doc = "The dut_control binary to use.",
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@hdctools//:dut_control"),
        ),
    },
    executable = True,
)
