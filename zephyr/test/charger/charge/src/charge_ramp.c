/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_ramp.h"
#include "system.h"
#include "system_fake.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, system_is_locked);

static void charge_ramp_before(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(system_is_locked);
}

static void charge_ramp_after(void *state)
{
	ARG_UNUSED(state);
	RESET_FAKE(system_is_locked);
}

/* Test ramping logic.
 *
 * Not testing BC1.2 chargers because driver tests should cover them.
 */
ZTEST_USER(charge_ramp, test_ramp)
{
	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_PD), 0);
	zassert_equal(chg_ramp_max(0, CHARGE_SUPPLIER_PD, 1234), 0);

	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_TYPEC), 0);
	zassert_equal(chg_ramp_max(0, CHARGE_SUPPLIER_TYPEC, 1234), 0);

	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_TYPEC_DTS), 1);
	zassert_equal(chg_ramp_max(0, CHARGE_SUPPLIER_TYPEC_DTS, 1234), 1234);
}

/* Disable ramping in locked RO */
ZTEST_USER(charge_ramp, test_ramp_locked)
{
	enum ec_image old_image = system_get_shrspi_image_copy();

	system_set_shrspi_image_copy(EC_IMAGE_RO);
	zassert_false(system_is_in_rw());

	system_is_locked_fake.return_val = 1;

	zassert_equal(chg_ramp_allowed(0, CHARGE_SUPPLIER_TYPEC_DTS), 0);

	system_set_shrspi_image_copy(old_image);
}

ZTEST_SUITE(charge_ramp, NULL, NULL, charge_ramp_before, charge_ramp_after,
	    NULL);
