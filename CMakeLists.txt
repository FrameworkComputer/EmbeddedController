# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Note: this cmake system implements only a zephyr module, and is not
# intended to build a complete EC.  To build projects in platform/ec,
# you should continue to use the Makefile system.
#
# Googlers can find the design doc at go/zephyr-shim.

if(NOT DEFINED ZEPHYR_CURRENT_MODULE_DIR)
  message(FATAL_ERROR "This Cmake system implements only a Zephyr module, and
          should not be invoked directly.  Please continue to use the Makefile
          system for non-Zephyr builds.")
endif()

set(PLATFORM_EC "${ZEPHYR_CURRENT_MODULE_DIR}" CACHE PATH
    "Path to the platform/ec repo.")

set(PLATFORM_EC_PROGRAM_DIR "${PLATFORM_EC}/zephyr/program" CACHE PATH
  "Path to the root directory containing all Zephyr EC programs and projects.")

if(NOT EXISTS "${PLATFORM_EC}/zephyr/module.yml")
  message(FATAL_ERROR "Failed to resolve PLATFORM_EC at
          ${PLATFORM_EC}/zephyr/module.yml")
endif()

add_subdirectory(zephyr)
add_subdirectory(common)
add_subdirectory(third_party)

# The EXISTS behavior is only well defined for full paths.
get_filename_component(private_repo_abs_path "private/fingerprint" ABSOLUTE)
if (EXISTS "${private_repo_abs_path}")
  add_subdirectory(private/fingerprint)
endif()
