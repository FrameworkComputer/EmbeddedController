/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_usbc_power_contract.h"

#include <zephyr/fff.h>

/* FFF fake definitions for select functions in `usbc_power_contract.c` */
DEFINE_FAKE_VALUE_FUNC(int, dpm_get_source_pdo, const uint32_t, const int);

void helper_reset_usbc_power_contract_fakes(void)
{
	RESET_FAKE(dpm_get_source_pdo);
}
