# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

# USAGE FAULT on int overflow
zephyr_compile_options(-ftrapv)

# Include directory with POSIX headers from Zephyr.
# We could enable CONFIG_POSIX_API, but it selects so many features that are
# not necessary.
zephyr_include_directories(${ZEPHYR_BASE}/include/zephyr/posix)

set(EXPECTED_ROLLBACK_VERSION 1)

if (DEFINED CONFIG_BOARD_BLOONCHIPPER)
	# Expected rollback version is 0 on bloonchipper.
	set(EXPECTED_ROLLBACK_VERSION 0)
	add_subdirectory(bloonchipper)
	project(bloonchipper)
endif()

if (DEFINED CONFIG_BASEBOARD_HELIPILOT)
	# Expected rollback version is 0 on helipilot.
	set(EXPECTED_ROLLBACK_VERSION 0)
	add_subdirectory(helipilot)
	project(helipilot)
endif()

if (DEFINED CONFIG_BOARD_ET171)
	add_subdirectory(et171)
	project(et171)
endif()

if (DEFINED CONFIG_BOARD_EM32F967)
	add_subdirectory(em32f967)
	project(em32f967)
endif()

if (DEFINED CONFIG_BOARD_FT9001)
	add_subdirectory(ft9001)
	project(ft9001)
endif()

if(NOT CONFIG_PLATFORM_EC_ROLLBACK_VERSION EQUAL EXPECTED_ROLLBACK_VERSION)
	message(FATAL_ERROR "Rollback version has changed! Expected ${EXPECTED_ROLLBACK_VERSION}, but ${CONFIG_PLATFORM_EC_ROLLBACK_VERSION} is set.")
endif()

zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD "src/fp_transport.c")
