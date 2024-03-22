/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "hooks.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(enum ec_error_list, charger_set_frequency, int);
FAKE_VALUE_FUNC(int, extpower_is_present);
FAKE_VALUE_FUNC(int, charge_get_percent);

static void reset(void)
{
	RESET_FAKE(charger_set_frequency);
	RESET_FAKE(extpower_is_present);
	RESET_FAKE(charge_get_percent);
}

static void alt_charger_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset();
}

ZTEST_SUITE(alt_charger, NULL, NULL, alt_charger_before, NULL, NULL);

static void validate_1000khz(enum hook_type type)
{
	hook_notify(type);

	zassert_equal(charger_set_frequency_fake.call_count, 1);
	zassert_equal(charger_set_frequency_fake.arg0_val, 1000);
}

static void validate_635khz(enum hook_type type)
{
	extpower_is_present_fake.return_val = 1;
	charge_get_percent_fake.return_val = 100;

	hook_notify(type);

	zassert_equal(charger_set_frequency_fake.call_count, 1);
	zassert_equal(charger_set_frequency_fake.arg0_val, 635);
}

ZTEST(alt_charger, test_resume)
{
	validate_1000khz(HOOK_CHIPSET_RESUME);
}

ZTEST(alt_charger, test_suspend)
{
	validate_1000khz(HOOK_CHIPSET_SUSPEND);

	reset();
	validate_635khz(HOOK_CHIPSET_SUSPEND);
}

ZTEST(alt_charger, test_shutdown)
{
	validate_1000khz(HOOK_CHIPSET_SHUTDOWN);

	reset();
	validate_635khz(HOOK_CHIPSET_SHUTDOWN);
}
