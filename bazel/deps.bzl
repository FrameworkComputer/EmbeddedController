# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _ec_deps_impl(module_ctx):
    def _coreboot_sdk_subtool(arch, version, sha256):
        http_archive(
            name = "ec-coreboot-sdk-%s" % arch,
            build_file = "//platform/rules_cros_firmware/cros_firmware:BUILD.gcs_subtool",
            sha256 = sha256,
            url = "https://storage.googleapis.com/chromiumos-sdk/toolchains/coreboot-sdk-%s/%s.tar.zst" % (arch, version),
        )

    _coreboot_sdk_subtool(
        "nds32le-elf",
        "11.3.0-r2/47a9bb6b7ef1ea584ed24078ea152a87204a37e1",
        "7299ae598233876ec2f562a1173f8b65de169b1de51cb6e04e003edfb4d04fe7",
    )
    _coreboot_sdk_subtool(
        "i386-elf",
        "11.3.0-r2/5ba88fb0227c76584851bd9cbb24d785e31a717b",
        "72f0b55516120e0919f10ddf28c53a429ccc8132685b6dbd6a8dcefeba92fcc5",
    )
    _coreboot_sdk_subtool(
        "arm-eabi",
        "11.3.0-r2/8adade1392d87565482ea57bfafaf74223cebbe5",
        "312557355983bf732b20dcf7b7553a5b2a13247fc3ad6f21226ff260db1783cd",
    )
    _coreboot_sdk_subtool(
        "riscv-elf",
        "11.3.0-r2/c97eb9fef0cf77f9d58d890de4e3e67f5158166f",
        "2345cfbf3dffd2efe0cdfa8d6100a0923d2dc8b77da9d98b9fd07200b18abec1",
    )

    return module_ctx.extension_metadata(
        root_module_direct_deps = [
            "ec-coreboot-sdk-arm-eabi",
            "ec-coreboot-sdk-i386-elf",
            "ec-coreboot-sdk-nds32le-elf",
            "ec-coreboot-sdk-riscv-elf",
        ],
        root_module_direct_dev_deps = [],
        reproducible = True,
    )

ec_deps = module_extension(
    implementation = _ec_deps_impl,
)
