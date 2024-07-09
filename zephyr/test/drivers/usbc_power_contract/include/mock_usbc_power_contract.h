/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <zephyr/fff.h>

/* FFF fake declarations for select functions in `usbc_power_contract.c` */
DECLARE_FAKE_VALUE_FUNC(int, dpm_get_source_pdo, const uint32_t, const int);
