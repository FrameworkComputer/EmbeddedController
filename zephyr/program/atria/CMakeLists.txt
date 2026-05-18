# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_ATRIARVP_RTK)
  project(atriarvp-rtk)
  zephyr_library_sources("src/rvp_board_power.c")
  zephyr_include_directories_ifdef(CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT
    "../intelrvp/include")
  zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DEDICATED_CHARGE_PORT
    "../intelrvp/src/dc_jack.c")

endif()

zephyr_include_directories("include/")

# TODO(b/501105189): Selection of ISH-specific config files happens in BUILD.py.
# Right now, there is only one atria ISH project, and there are no ISH-specific
# C files. Including this subdirectory is a no-op. Eventually, there will be
# multiple atria ISH projects, but they will have highly overlapping configs.
# Refactoring the ISH directory organization and this CMake section could make
# it easier to reuse those configs.
if(DEFINED CONFIG_BOARD_INTEL_ISH_5_8_0)
  project(atriarvp-ish)
endif()

add_subdirectory(${PROJECT_NAME})
