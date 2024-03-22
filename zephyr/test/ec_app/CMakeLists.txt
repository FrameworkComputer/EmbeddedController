# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(ec_app)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
