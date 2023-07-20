# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(system_shim_test)

# Include FFF fakes
add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)

target_sources(app PRIVATE src/pm_handlers.c)

if(CONFIG_SYSTEM_SHIM_TEST_ISH)
  include("${CMAKE_CURRENT_LIST_DIR}/test_ish.cmake")
elseif(CONFIG_SYSTEM_SHIM_TEST_DEFAULT)
  include("${CMAKE_CURRENT_LIST_DIR}/test_default.cmake")
endif()
