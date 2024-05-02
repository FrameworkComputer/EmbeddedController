/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#define EC_RESET_FLAG_RESET_PIN BIT(1)

int system_jumped_late(void);
uint32_t system_get_reset_flags(void);
