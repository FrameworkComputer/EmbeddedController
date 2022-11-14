/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "fakes.h"
#include "system.h"

ZTEST(system, test_fail_get_bbram_no_device)
{
	zassert_equal(EC_ERROR_INVAL, system_get_bbram(0, NULL));
}

ZTEST(system, test_fail_set_scratchpad)
{
	zassert_equal(-EC_ERROR_INVAL, system_set_scratchpad(0));
}

ZTEST(system, test_fail_get_scratchpad)
{
	zassert_equal(-EC_ERROR_INVAL, system_get_scratchpad(NULL));
}
