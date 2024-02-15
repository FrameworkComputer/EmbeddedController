/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "extpower.h"
#include "mock/isl923x.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(enum ec_error_list, raa489000_is_acok, int, bool *);

static void test_before(void *fixture)
{
	RESET_FAKE(raa489000_is_acok);
}

ZTEST_SUITE(isl923x_extpower, NULL, NULL, test_before, NULL, NULL);

ZTEST(isl923x_extpower, test_extpower_error)
{
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_error;
	zassert_false(extpower_is_present());
	zassert_not_equal(raa489000_is_acok_fake.call_count, 0);
}

ZTEST(isl923x_extpower, test_extpower_absent)
{
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_absent;
	zassert_false(extpower_is_present());
	zassert_not_equal(raa489000_is_acok_fake.call_count, 0);
}

ZTEST(isl923x_extpower, test_extpower_present)
{
	raa489000_is_acok_fake.custom_fake = raa489000_is_acok_present;
	zassert_true(extpower_is_present());
	zassert_not_equal(raa489000_is_acok_fake.call_count, 0);
}
