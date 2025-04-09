# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

file(SHA256 ${INPUT_FILE} npcx_monitor_hash)

if ("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "coreboot-sdk")
  # Coreboot builds
  set(expected_hash "331629c64ef65caef1b23f0f29474f13d9b41447408a70b6e4260310801d324d")
elseif ("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "zephyr")
  # Zephyr builds
  set(expected_hash "2a4abad66294d175ad82542f5be35af3802d9df5e12c58c9835a15e0886f78dd")
elseif ("${ZEPHYR_TOOLCHAIN_VARIANT}" STREQUAL "llvm")
  # llvm builds - skip the hash check.  We currently deploy the version
  # built with the coreboot-sdk toolchain.
  message(STATUS "NPCX Monitor hash check skipped")
  return()
endif()

if("${npcx_monitor_hash}" STREQUAL "${expected_hash}")
  message(STATUS "NPCX Monitor hash match")
else()
  message(FATAL_ERROR
      "NPCX monitor expected hash ${expected_hash}\n"
      "NPCX monitor actual hash ${npcx_monitor_hash}\n"
      "NPCX monitor change detected. See README.md for instructions."
  )
endif()
