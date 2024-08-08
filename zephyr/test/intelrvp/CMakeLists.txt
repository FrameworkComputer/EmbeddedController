# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(intelrvp)

add_subdirectory(tests)
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
zephyr_include_directories(${PLATFORM_EC}/zephyr/mock/include)
zephyr_include_directories(${PLATFORM_EC}/zephyr/subsys/ap_pwrseq/include)
