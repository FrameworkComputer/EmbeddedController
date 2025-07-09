/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_FPC1145_SRC_TEST_HELPERS_H_
#define ZEPHYR_TEST_DRIVERS_FPC1145_SRC_TEST_HELPERS_H_

#include <zephyr/kernel.h>

#include <fingerprint_fpc1145_pal.h>

__syscall int fpc1145_pal_spi_writeread(fpc_device_t device, uint8_t *tx_buffer,
					uint8_t *rx_buffer, uint32_t size);
__syscall int fpc1145_pal_wait_irq(fpc_device_t device,
				   enum fpc_pal_irq irq_type);
__syscall int fpc1145_pal_get_time(uint64_t *time_us);
__syscall int fpc1145_pal_delay_us(uint64_t us);

#include <zephyr/syscalls/fpc1145_pal_test_helpers.h>

#endif /* ZEPHYR_TEST_DRIVERS_FPC1145_SRC_TEST_HELPERS_H_ */
