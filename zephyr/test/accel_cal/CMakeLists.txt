# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(accel_cal)

# Ensure that we get the definitions from test_config.h
zephyr_compile_definitions("TEST_ACCEL_CAL")

# Include the local test directory for shimmed_test_tasks.h
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# Include test file and unit under test
target_sources(app PRIVATE
	"${PLATFORM_EC}/test/accel_cal.c"
	"${PLATFORM_EC}/common/accel_cal.c"
	"${PLATFORM_EC}/common/kasa.c"
	"${PLATFORM_EC}/common/mat44.c"
	"${PLATFORM_EC}/common/math_util.c"
	"${PLATFORM_EC}/common/newton_fit.c"
	"${PLATFORM_EC}/common/stillness_detector.c"
	"${PLATFORM_EC}/common/vec3.c")

zephyr_library_sources(${PLATFORM_EC}/zephyr/test/test_utils/tasks_fakes.c)
zephyr_library_sources(${PLATFORM_EC}/zephyr/test/test_utils/fff.c)
