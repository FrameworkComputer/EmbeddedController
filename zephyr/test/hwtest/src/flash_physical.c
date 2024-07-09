/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(flash_physical, NULL, NULL, NULL, NULL, NULL);

#ifdef CONFIG_SOC_FAMILY_STM32
/* Covered by Zephyr tests. "flash_stm32" suite for STM32 chips */
ZTEST(flash_physical, test_flash_physical)
{
	zassert_equal(1, 1);
}
#else
#error "Flash tests not defined for this chip. Please add it."
#endif
