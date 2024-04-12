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
zephyr_library_sources_ifdef(CONFIG_HW_TEST_FP_TRANSPORT src/fp_transport.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_FTRAPV src/ftrapv.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_LIBC_PRINTF src/libc_printf.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_MALLOC src/malloc.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_PANIC src/panic.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_PRINTF src/printf.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_QUEUE src/queue.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_RESTRICTED_CONSOLE ${PLATFORM_EC}/zephyr/test/console/src/console_restricted.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_ROLLBACK src/rollback.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_ROLLBACK_ENTROPY src/rollback_entropy.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_SHA256 ${PLATFORM_EC}/zephyr/test/crypto/src/sha256.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_SHA256_UNROLLED ${PLATFORM_EC}/zephyr/test/crypto/src/sha256.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_STATIC_IF ${PLATFORM_EC}/zephyr/test/utils/src/static_if.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_SYSTEM_IS_LOCKED src/system_is_locked.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_UTILS ${PLATFORM_EC}/zephyr/test/utils/src/utils.c)
zephyr_library_sources_ifdef(CONFIG_HW_TEST_UTILS_STR ${PLATFORM_EC}/zephyr/test/utils/src/utils_str.c)
