# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
import mock
import zmake.toolchains as toolchains


def test_coreboot_sdk():
    config = toolchains.get_toolchain('coreboot-sdk', {'ec': pathlib.Path('/')})
    assert config.cmake_defs['ZEPHYR_TOOLCHAIN_VARIANT'] == 'coreboot-sdk'
    assert config.cmake_defs['TOOLCHAIN_ROOT'] == '/zephyr'


def test_llvm():
    config = toolchains.get_toolchain('llvm', {'ec': pathlib.Path('/')})
    assert config.cmake_defs['ZEPHYR_TOOLCHAIN_VARIANT'] == 'llvm'
    assert config.cmake_defs['TOOLCHAIN_ROOT'] == '/zephyr'


@mock.patch('zmake.toolchains.find_zephyr_sdk', return_value='/opt/zephyr-sdk')
def test_zephyr(find_zephyr_sdk):
    config = toolchains.get_toolchain('zephyr', {})
    assert config.cmake_defs['ZEPHYR_TOOLCHAIN_VARIANT'] == 'zephyr'
    assert config.cmake_defs['ZEPHYR_SDK_INSTALL_DIR'] == '/opt/zephyr-sdk'
    assert config.environ_defs['ZEPHYR_SDK_INSTALL_DIR'] == '/opt/zephyr-sdk'


def test_arm_none_eabi():
    config = toolchains.get_toolchain('arm-none-eabi', {})
    assert config.cmake_defs['ZEPHYR_TOOLCHAIN_VARIANT'] == 'cross-compile'
    assert config.cmake_defs['CROSS_COMPILE'] == '/usr/bin/arm-none-eabi-'
