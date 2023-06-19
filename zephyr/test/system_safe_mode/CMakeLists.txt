# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(system_safe_mode)

# Include FFF fakes
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources_ifdef(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE app PRIVATE "src/system_safe_mode.c")
target_sources_ifndef(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE app PRIVATE "src/system_safe_mode_disabled.c")
