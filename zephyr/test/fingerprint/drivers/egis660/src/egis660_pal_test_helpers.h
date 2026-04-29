/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_EGIS660_SRC_TEST_HELPERS_H_
#define ZEPHYR_TEST_DRIVERS_EGIS660_SRC_TEST_HELPERS_H_

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

/* Flash geometry — matches config_chip.h for the PAL */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE \
	DT_PROP(DT_NODELABEL(flash0), write_block_size)
#define CONFIG_FLASH_ERASE_SIZE DT_PROP(DT_NODELABEL(flash0), erase_block_size)

/* Simulated APNS storage area within the flash (64KB at 1MB offset) */
#define APNS_TEST_ADDR 0x100000
#define APNS_TEST_SIZE (64 * 1024)

__syscall int egis660_pal_spi_write_read(uint8_t *data, size_t write_size,
					 size_t read_size,
					 bool leave_cs_asserted);
__syscall bool egis660_pal_check_irq(void);
__syscall bool egis660_pal_read_irq(void);
__syscall void egis660_pal_reset(bool state);
__syscall uint32_t egis660_pal_timebase_get_tick(void);
__syscall void egis660_pal_timebase_delay_us(uint32_t us);
__syscall void egis660_pal_timebase_delay_ms(uint32_t ms);
__syscall void *egis660_pal_malloc(uint32_t size);
__syscall void egis660_pal_free(void *data);

/* Flash test helpers — wrap Zephyr flash API for the APNS partition */
__syscall int egis660_pal_flash_erase(uint32_t offset, size_t size);
__syscall int egis660_pal_flash_read(uint32_t offset, size_t size, void *data);
__syscall int egis660_pal_flash_write(uint32_t offset, const void *data,
				      size_t size);

#include <zephyr/syscalls/egis660_pal_test_helpers.h>

#endif /* ZEPHYR_TEST_DRIVERS_EGIS660_SRC_TEST_HELPERS_H_ */
