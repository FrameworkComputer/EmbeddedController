/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "system.h"

/* Tests for !CONFIG_HOST_CMD_AP_SET_SKUID */

#define DEFAULT_BOARD_PROVIDED_SKU_ID 0

ZTEST(system, test_system_get_sku_id_default_board_provided_value)
{
	zassert_equal(system_get_sku_id(), DEFAULT_BOARD_PROVIDED_SKU_ID);
}
