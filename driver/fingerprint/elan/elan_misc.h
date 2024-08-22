/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ELAN Platform Abstraction Layer callbacks */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_ELAN_ELAN_MISC_H_
#define __CROS_EC_DRIVER_FINGERPRINT_ELAN_ELAN_MISC_H_

#include "timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate memory
 *
 * @param size Allocation size
 * @return Address on successful allocation, panic otherwise
 */
__staticlib_hook void *elan_malloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 *
 * @param data Pointer to buffer that should be freed
 */
__staticlib_hook void elan_free(void *data);

/**
 * @brief Output console message
 *
 * @param format Pointer to buffer that should be output console
 */
__staticlib_hook void elan_log_var(const char *format, ...);

/**
 * @brief Reads the system tick counter.
 *
 * @return Tick count since system startup. [ms]
 */
__staticlib_hook uint32_t elan_get_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_ELAN_ELAN_MISC_H__ */
