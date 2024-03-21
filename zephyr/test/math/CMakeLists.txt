# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(math)

target_sources(app PRIVATE ${PLATFORM_EC}/common/math_util.c)

target_sources(
	app PRIVATE
		src/suite.c
		src/math_util.c
		src/vector.c
		src/mask.c
)

# Fixed point specific tests
target_sources_ifndef(CONFIG_FPU app PRIVATE src/fixed_point_int_sqrtf.c)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
