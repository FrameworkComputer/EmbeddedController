/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_ELAN80SERIES_SRC_TEST_HELPERS_H_
#define ZEPHYR_TEST_DRIVERS_ELAN80SERIES_SRC_TEST_HELPERS_H_

#include <zephyr/kernel.h>

#include <fingerprint_elan80series_pal.h>

__syscall int elan80series_pal_usleep(unsigned int us);
__syscall void *elan80series_pal_malloc(uint32_t size);
__syscall void elan80series_pal_free(void *data);
__syscall uint32_t elan80series_pal_get_tick(void);
__syscall void elan80series_pal_sensor_set_rst(bool state);
__syscall int elan80series_pal_read_register(uint8_t regaddr, uint8_t *regdata);
__syscall int elan80series_pal_read_cmd(uint8_t fp_cmd, uint8_t *regdata);

#include <zephyr/syscalls/elan80series_pal_test_helpers.h>

#endif /* ZEPHYR_TEST_DRIVERS_ELAN80SERIES_SRC_TEST_HELPERS_H_ */
