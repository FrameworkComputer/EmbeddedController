# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

zephyr_library_sources("src/main.c")
zephyr_library_sources_ifdef(CONFIG_HW_TEST_ABORT src/abort.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_CORTEXM_FPU src/cortexm_fpu.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_CRC ${PLATFORM_EC}/zephyr/test/crc/main.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_FLASH_PHYSICAL src/flash_physical.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_FLASH_WRITE_PROTECT src/flash_write_protect.c)
