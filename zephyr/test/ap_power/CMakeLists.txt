# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(ap_power)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

# Include the local test directory for shimmed_test_tasks.h
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(app PRIVATE
  src/ap_pwrseq.c
  src/console_command.c
  src/events.c
  src/hibdelay.c
  src/hibernate.c
  src/host_command.c
	src/test_mocks.c
  src/main.c)

# Generic architecture tests
target_sources_ifdef(CONFIG_TEST_AP_POWER_ADL app PRIVATE
  src/boards/alderlake.c src/signals.c)

target_sources_ifdef(CONFIG_TEST_AP_POWER_MTL app PRIVATE
  src/boards/meteorlake.c)

target_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_CONSOLE app PRIVATE
  src/pwrseq_console.c)

# Project specific tests
target_sources_ifdef(CONFIG_TEST_AP_POWER_BROX app PRIVATE
  ${PLATFORM_EC_PROGRAM_DIR}/brox/src/board_power.c)
