# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(charger)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

add_subdirectory_ifdef(CONFIG_TEST_ISL923X isl923x)

if (DEFINED CONFIG_TEST_CHARGER_SOLO_DEF)
	target_compile_definitions(app PRIVATE CHARGER_SOLO=0)
endif()

if (DEFINED CONFIG_TEST_CHARGER_MULTI_DEF)
	target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
endif()

target_sources(app PRIVATE src/mocks.c)

if (CONFIG_TEST_SRC STREQUAL "")
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/${CONFIG_TEST_PROGRAM_NAME}/${CONFIG_TEST_BOARD_NAME}/src/charger.c)
else()
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/${CONFIG_TEST_PROGRAM_NAME}/${CONFIG_TEST_SRC})
endif()
