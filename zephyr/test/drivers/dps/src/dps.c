/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "dps.h"
#include "test/drivers/test_state.h"

struct dps_fixture {
	struct dps_config_t saved_config;
};

static void *dps_config_setup(void)
{
	static struct dps_fixture fixture;

	fixture.saved_config = *dps_get_config();

	return &fixture;
}

static void dps_config_after(void *data)
{
	*dps_get_config() = ((struct dps_fixture *)data)->saved_config;
	dps_enable(true);
}

ZTEST_F(dps, test_enable)
{
	zassert_true(dps_is_enabled(), NULL);
	dps_enable(false);
	zassert_false(dps_is_enabled(), NULL);
	dps_enable(true);
	zassert_true(dps_is_enabled(), NULL);
}

ZTEST_F(dps, test_config)
{
	struct dps_config_t *config = dps_get_config();

	zassert_true(config->k_less_pwr <= config->k_more_pwr, NULL);
	zassert_true(config->k_less_pwr > 0 && config->k_less_pwr < 100, NULL);
	zassert_true(config->k_more_pwr > 0 && config->k_more_pwr < 100, NULL);

	zassert_ok(dps_init(), NULL);
	*config = fixture->saved_config;

	config->k_less_pwr = config->k_more_pwr + 1;
	zassert_equal(dps_init(), EC_ERROR_INVALID_CONFIG, NULL);
	*config = fixture->saved_config;

	config->k_more_pwr = 101;
	zassert_equal(dps_init(), EC_ERROR_INVALID_CONFIG, NULL);
	*config = fixture->saved_config;
}

ZTEST_SUITE(dps, drivers_predicate_pre_main, dps_config_setup, NULL,
	    dps_config_after, NULL);
