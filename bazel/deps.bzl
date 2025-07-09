# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _ec_deps_impl(module_ctx):
    def _coreboot_sdk_subtool(arch, version, sha256, bucket = "chromiumos-sdk"):
        name = "ec-coreboot-sdk-%s" % arch
        toolchain_name = "coreboot-sdk-%s" % arch
        http_archive(
            name = name,
            build_file = "//platform/rules_cros_firmware/cros_firmware:BUILD.gcs_subtool",
            sha256 = sha256,
            url = "https://storage.googleapis.com/%s/toolchains/%s/%s.tar.zst" % (bucket, toolchain_name, version),
        )

    script = module_ctx.path(Label("@@//platform/ec/util:coreboot_sdk_portage_deps.py"))
    script_output = module_ctx.execute([script])

    direct_deps = []
    for toolchain in script_output.stdout.splitlines():
        arch, bucket, version, name = toolchain.split(" ")
        direct_deps.append("ec-coreboot-sdk-" + arch)
        _coreboot_sdk_subtool(arch, "%s/%s" % (version, name), "", bucket)

    return module_ctx.extension_metadata(
        root_module_direct_deps = direct_deps,
        root_module_direct_dev_deps = [],
        reproducible = True,
    )

ec_deps = module_extension(
    implementation = _ec_deps_impl,
)
