# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(rwsig)

zephyr_include_directories(include)

target_sources(app PRIVATE
	src/rwsig.c
)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
