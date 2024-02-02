# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(pdc)

# Include FFF fakes
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/fakes.c)

if (DEFINED CONFIG_TEST_SUITE_PDC_GENERIC)
	target_sources(app PRIVATE src/generic/pdc_api.c)
	target_sources(app PRIVATE src/generic/pdc_power_mgmt_api.c)
endif()
