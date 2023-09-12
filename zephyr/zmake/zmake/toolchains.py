# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Definitions of toolchain variables."""

import os
import pathlib

from zmake import build_config


class GenericToolchain:
    """Default toolchain if not known to zmake.

    Simply pass ZEPHYR_TOOLCHAIN_VARIANT=name to the build, with
    nothing extra.
    """

    def __init__(self, name, modules=None):
        self.name = name
        self.modules = modules or {}

    def probe(self):  # pylint: disable=no-self-use
        """Probe if the toolchain is available on the system."""
        # Since the toolchain is not known to zmake, we have no way to
        # know if it's installed.  Simply return False to indicate not
        # installed.  An unknown toolchain would only be used if -t
        # was manually passed to zmake, and is not valid to put in a
        # BUILD.py file.
        return False

    def get_build_config(self):
        """Get the build configuration for the toolchain.

        Returns:
            A build_config.BuildConfig to be applied to the build.
        """
        return build_config.BuildConfig(
            cmake_defs={
                "ZEPHYR_TOOLCHAIN_VARIANT": self.name,
            },
        )


class CorebootSdkToolchain(GenericToolchain):
    """Coreboot SDK toolchain installed in default location."""

    @staticmethod
    def _find_coreboot_sdk():
        """Find coreboot-sdk, if available.

        Returns:
            The path to coreboot-sdk, or None if unavailable.
        """
        path = pathlib.Path(
            os.environ.get("COREBOOT_SDK_ROOT", "/opt/coreboot-sdk")
        ).resolve()
        if path.is_dir():
            return path
        return None

    def probe(self):
        return bool(self._find_coreboot_sdk())

    def get_build_config(self):
        return (
            build_config.BuildConfig(
                cmake_defs={
                    "TOOLCHAIN_ROOT": str(self.modules["ec"] / "zephyr"),
                    "COREBOOT_SDK_ROOT": str(self._find_coreboot_sdk()),
                },
            )
            | super().get_build_config()
        )


class ZephyrToolchain(GenericToolchain):
    """Zephyr SDK toolchain.

    Either set the environment var ZEPHYR_SDK_INSTALL_DIR, or install
    the SDK in one of the common known locations.
    """

    def __init__(self, *args, **kwargs):
        self.zephyr_sdk_install_dir = self._find_zephyr_sdk()
        super().__init__(*args, **kwargs)

    @staticmethod
    def _find_zephyr_sdk():
        """Find the Zephyr SDK, if it's installed.

        Returns:
            The path to the Zephyr SDK, using the search rules defined by
            https://docs.zephyrproject.org/latest/getting_started/installation_linux.html,
            or None, if one cannot be found on the system.
        """
        from_env = os.getenv("ZEPHYR_SDK_INSTALL_DIR")
        if from_env:
            return pathlib.Path(from_env)

        def _gen_sdk_paths():
            for prefix in (
                "~",
                "~/.local",
                "~/.local/opt",
                "~/bin",
                "/opt",
                "/usr",
                "/usr/local",
            ):
                prefix = pathlib.Path(os.path.expanduser(prefix))
                yield prefix / "zephyr-sdk"
                yield from prefix.glob("zephyr-sdk-*")

        for path in _gen_sdk_paths():
            if (path / "sdk_version").is_file():
                return path

        return None

    def probe(self):
        return bool(self.zephyr_sdk_install_dir)

    def get_build_config(self):
        if not self.zephyr_sdk_install_dir:
            raise RuntimeError(
                "No installed Zephyr SDK was found"
                " (see docs/zephyr/zephyr_build.md for documentation)"
            )
        tc_vars = {
            "ZEPHYR_SDK_INSTALL_DIR": str(self.zephyr_sdk_install_dir),
        }
        return (
            build_config.BuildConfig(cmake_defs=tc_vars)
            | super().get_build_config()
        )


class LlvmToolchain(GenericToolchain):
    """LLVM toolchain as used in the chroot."""

    def probe(self):
        # TODO: differentiate chroot llvm path vs. something more
        # generic?
        return pathlib.Path("/usr/bin/x86_64-pc-linux-gnu-clang").exists()

    def get_build_config(self):
        # TODO: this contains custom settings for the chroot.  Plumb a
        # toolchain for "generic-llvm" for external uses?
        return (
            build_config.BuildConfig(
                cmake_defs={
                    "TOOLCHAIN_ROOT": str(self.modules["ec"] / "zephyr"),
                },
            )
            | super().get_build_config()
        )


class HostToolchain(GenericToolchain):
    """GCC toolchain found in the PATH."""

    def probe(self):
        # "host" toolchain for Zephyr means GCC.
        for search_path in os.getenv("PATH", "/usr/bin").split(":"):
            if (pathlib.Path(search_path) / "gcc").exists():
                return True
        return False


# Mapping of toolchain names -> support class
support_classes = {
    "coreboot-sdk": CorebootSdkToolchain,
    "host": HostToolchain,
    "llvm": LlvmToolchain,
    "zephyr": ZephyrToolchain,
}
