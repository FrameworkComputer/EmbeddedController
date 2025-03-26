# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(charger)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

add_subdirectory_ifdef(CONFIG_TEST_ISL923X isl923x)

if (DEFINED CONFIG_TEST_CHARGER_MULTI_DEF)
	target_compile_definitions(app PRIVATE CHARGER_PRIMARY=0 CHARGER_SECONDARY=1)
endif()

target_sources(app PRIVATE
	src/main.c
	src/mocks.c
)

add_subdirectory_ifdef(CONFIG_LINK_TEST_SUITE_CHARGE charge)

if ( NOT DEFINED CONFIG_LINK_TEST_SUITE_CHARGE)
	if (CONFIG_TEST_SRC STREQUAL "")
		target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/${CONFIG_TEST_PROGRAM_NAME}/${CONFIG_TEST_BOARD_NAME}/src/charger.c)
	else()
		target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/${CONFIG_TEST_PROGRAM_NAME}/${CONFIG_TEST_SRC})
	endif()
endif()
