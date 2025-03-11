/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_EGIS630_SRC_TEST_HELPERS_H_
#define ZEPHYR_TEST_DRIVERS_EGIS630_SRC_TEST_HELPERS_H_

#include <zephyr/kernel.h>

#include <fingerprint_egis630_pal.h>

__syscall uint64_t egis630_plat_get_time(void);
__syscall void egis630_plat_wait_time(uint32_t mecs);
__syscall void egis630_plat_sleep_time(uint32_t timeInMs);
__syscall uint32_t egis630_plat_get_diff_time(uint64_t begin);
__syscall void *egis630_sys_alloc(size_t count, size_t size);
__syscall int egis630_periphery_spi_write_read(uint8_t *write, uint32_t size,
					       uint8_t *read, uint32_t rx_len);
__syscall void egis630_set_debug_level(LOG_LEVEL level);
__syscall int egis630_rbs_check_if_null(void *ptr, int error_code);

#include <zephyr/syscalls/egis630_pal_test_helpers.h>

#endif /* ZEPHYR_TEST_DRIVERS_EGIS630_SRC_TEST_HELPERS_H_ */
