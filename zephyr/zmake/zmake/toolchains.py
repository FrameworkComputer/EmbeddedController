# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of toolchain variables."""

import glob
import os
import pathlib

import zmake.build_config as build_config


def find_zephyr_sdk():
    """Find the Zephyr SDK, if it's installed.

    Returns:
        The path to the Zephyr SDK, using the search rules defined by
        https://docs.zephyrproject.org/latest/getting_started/installation_linux.html
    """

    def _gen_sdk_paths():
        yield os.getenv("ZEPHYR_SDK_INSTALL_DIR")

        for searchpath in (
            "~/zephyr-sdk",
            "~/.local/zephyr-sdk",
            "~/.local/opt/zephyr-sdk",
            "~/bin/zephyr-sdk",
            "/opt/zephyr-sdk",
            "/usr/zephyr-sdk",
            "/usr/local/zephyr-sdk",
        ):
            for suffix in ("", "-*"):
                yield from glob.glob(os.path.expanduser(searchpath + suffix))

    for path in _gen_sdk_paths():
        if not path:
            continue
        path = pathlib.Path(path)
        if (path / "sdk_version").is_file():
            return path

    raise OSError("Unable to find the Zephyr SDK")


# Mapping of toolchain names -> (λ (module-paths) build-config)
toolchains = {
    "coreboot-sdk": lambda modules: build_config.BuildConfig(
        cmake_defs={
            "TOOLCHAIN_ROOT": str(modules["ec"] / "zephyr"),
            "ZEPHYR_TOOLCHAIN_VARIANT": "coreboot-sdk",
        }
    ),
    "llvm": lambda modules: build_config.BuildConfig(
        cmake_defs={
            "TOOLCHAIN_ROOT": str(modules["ec"] / "zephyr"),
            "ZEPHYR_TOOLCHAIN_VARIANT": "llvm",
        }
    ),
    "zephyr": lambda _: build_config.BuildConfig(
        cmake_defs={
            "ZEPHYR_TOOLCHAIN_VARIANT": "zephyr",
            "ZEPHYR_SDK_INSTALL_DIR": str(find_zephyr_sdk()),
        },
        environ_defs={"ZEPHYR_SDK_INSTALL_DIR": str(find_zephyr_sdk())},
    ),
    "arm-none-eabi": lambda _: build_config.BuildConfig(
        cmake_defs={
            "ZEPHYR_TOOLCHAIN_VARIANT": "cross-compile",
            "CROSS_COMPILE": "/usr/bin/arm-none-eabi-",
        }
    ),
}


def get_toolchain(name, module_paths):
    """Get a toolchain by name.

    Args:
        name: The name of the toolchain.
        module_paths: Dictionary mapping module names to paths.

    Returns:
        The corresponding BuildConfig from the defined toolchains, if
        one exists, otherwise a simple BuildConfig which sets
        ZEPHYR_TOOLCHAIN_VARIANT to the corresponding name.
    """
    if name in toolchains:
        return toolchains[name](module_paths)
    return build_config.BuildConfig(cmake_defs={"ZEPHYR_TOOLCHAIN_VARIANT": name})
