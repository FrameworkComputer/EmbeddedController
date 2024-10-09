#!/usr/bin/env -S python3 -u
# -*- coding: utf-8 -*-
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provide coreboot-sdk to callers

Initialize the coreboot-sdk subtools and provide environment variables
to the caller to indicate the extracted location.
"""

import os
import subprocess


def init_toolchain():
    """Initialize coreboot-sdk.

    Returns:
        Environment variables to use for toolchain.
    """
    if os.environ.get("COREBOOT_SDK_ROOT") is not None:
        print("COREBOOT_SDK_ROOT already set by environment, returning")
        return {}

    # (environment variable, bazel target)
    toolchains = [
        ("COREBOOT_SDK_ROOT_arm", "@ec-coreboot-sdk-arm-eabi//:get_path"),
        ("COREBOOT_SDK_ROOT_x86", "@ec-coreboot-sdk-i386-elf//:get_path"),
        ("COREBOOT_SDK_ROOT_riscv", "@ec-coreboot-sdk-riscv-elf//:get_path"),
        ("COREBOOT_SDK_ROOT_nds32", "@ec-coreboot-sdk-nds32le-elf//:get_path"),
    ]

    subprocess.run(
        [
            "bazel",
            "--project",
            "fwsdk",
            "build",
            *(target for _, target in toolchains),
        ],
        check=True,
    )

    result = {}
    for name, target in toolchains:
        run_result = subprocess.run(
            ["bazel", "--project", "fwsdk", "run", target],
            check=True,
            stdout=subprocess.PIPE,
        )
        result[name] = run_result.stdout.strip()
    return result


if __name__ == "__main__":
    env_vars = init_toolchain()
    # Return a formatted string which can be declared as an associative array in bash
    print(
        " ".join(
            f"[{key}]={value.decode('utf-8')}"
            for key, value in env_vars.items()
        )
    )
