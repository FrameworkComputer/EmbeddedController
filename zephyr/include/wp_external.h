/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_WP_EXTERNAL_H
#define __CROS_EC_ZEPHYR_WP_EXTERNAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool write_protect_is_asserted_external(void);
void disable_write_protect_external(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_WP_EXTERNAL_H */
