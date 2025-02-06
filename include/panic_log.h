/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PANIC_LOG_H
#define __CROS_EC_PANIC_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Write a character to the panic log. Character will be dropped if panic log is
 * frozen. */
void panic_log_write_char(const char c);

/* Write a string to the panic log. String will be dropped if panic log is
 * frozen. */
void panic_log_write_str(const char *str, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PANIC_LOG_H */