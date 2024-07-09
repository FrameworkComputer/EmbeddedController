# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(crypto)

target_sources_ifdef(CONFIG_TEST_SHA256 app PRIVATE src/sha256.c)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
