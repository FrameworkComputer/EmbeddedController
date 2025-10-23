/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_FT98XX_SRC_TEST_HELPERS_H_
#define ZEPHYR_TEST_DRIVERS_FT98XX_SRC_TEST_HELPERS_H_

#include <zephyr/kernel.h>

#include <fingerprint_ft98xx_pal.h>

__syscall int ft98xx_sensor_hw_reset(void);
__syscall int ft98xx_spi_write(uint8_t *buffer, uint32_t len);
__syscall int ft98xx_spi_write_then_read(uint8_t *tx_buffer, uint32_t tx_len,
					 uint8_t *rx_buffer, uint32_t rx_len);
__syscall void *ft98xx_focal_malloc(uint32_t size);
__syscall void ft98xx_focal_free(void *data);

#include <zephyr/syscalls/ft98xx_pal_test_helpers.h>

#endif /* ZEPHYR_TEST_DRIVERS_FT98XX_SRC_TEST_HELPERS_H_ */
