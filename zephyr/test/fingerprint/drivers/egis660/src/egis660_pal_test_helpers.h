/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_EGIS660_SRC_TEST_HELPERS_H_
#define ZEPHYR_TEST_DRIVERS_EGIS660_SRC_TEST_HELPERS_H_

#include <zephyr/kernel.h>

__syscall int egis660_pal_spi_write_read(uint8_t *data, size_t write_size,
					 size_t read_size,
					 bool leave_cs_asserted);
__syscall bool egis660_pal_check_irq(void);
__syscall bool egis660_pal_read_irq(void);
__syscall void egis660_pal_reset(bool state);
__syscall uint32_t egis660_pal_timebase_get_tick(void);
__syscall void egis660_pal_timebase_delay_ms(uint32_t ms);
__syscall void *egis660_pal_malloc(uint32_t size);
__syscall void egis660_pal_free(void *data);

#include <zephyr/syscalls/egis660_pal_test_helpers.h>

#endif /* ZEPHYR_TEST_DRIVERS_EGIS660_SRC_TEST_HELPERS_H_ */
