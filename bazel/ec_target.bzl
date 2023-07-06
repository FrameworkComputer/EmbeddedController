# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load(
    "@cros_firmware//platform/rules_cros_firmware/cros_firmware:ec.bzl",
    "ec_binary",
)
load(
    "@cros_firmware//platform/ec/bazel:flash_ec.bzl",
    "flash_ec",
)

def ec_target(
        name,
        extra_modules = []):
    ec_binary(name = name, extra_modules = extra_modules)
    flash_ec(
        name = "flash_ec_{}".format(name),
        board = name,
        build_target = "@cros_firmware//platform/ec:{}".format(name),
    )
