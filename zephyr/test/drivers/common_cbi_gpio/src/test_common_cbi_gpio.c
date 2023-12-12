/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, system_get_board_version);
FAKE_VALUE_FUNC(int, cbi_set_board_info, enum cbi_data_tag, const uint8_t *,
		uint8_t);

ZTEST(cbi_gpio, test_cbio_is_write_protected)
{
	zassert_true(cbi_config->drv->is_protected());
}

ZTEST(cbi_gpio, test_cbi_gpio_read__negative_board_id)
{
	uint8_t unused_offset = 0;
	uint8_t unused_data = 0;
	uint8_t unused_len = 0;

	system_get_board_version_fake.return_val = -1;
	zassert_equal(cbi_config->drv->load(unused_offset, &unused_data,
					    unused_len),
		      EC_ERROR_UNKNOWN);
}

ZTEST(cbi_gpio, test_cbi_gpio_read__bad_board_info_set)
{
	uint8_t unused_offset = 0;
	uint8_t unused_data = 0;
	uint8_t unused_len = 0;

	/* Arbitrary nonzero to indicate failure */
	cbi_set_board_info_fake.return_val = 1;
	zassert_equal(cbi_config->drv->load(unused_offset, &unused_data,
					    unused_len),
		      EC_ERROR_UNKNOWN);
}

ZTEST(cbi_gpio, test_cbi_gpio_read__negative_board_id_then_bad_board_info_set)
{
	/* Tests the path of two separate errors occurring */

	uint8_t unused_offset = 0;
	uint8_t unused_data = 0;
	uint8_t unused_len = 0;

	system_get_board_version_fake.return_val = -1;

	/* Arbitrary nonzero to indicate failure */
	cbi_set_board_info_fake.return_val = 1;
	zassert_equal(cbi_config->drv->load(unused_offset, &unused_data,
					    unused_len),
		      EC_ERROR_UNKNOWN);
}

static void test_cbi_gpio_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	RESET_FAKE(system_get_board_version);
	/* Make each cbi fetch fresh */
	cbi_invalidate_cache();
}

ZTEST_SUITE(cbi_gpio, drivers_predicate_post_main, NULL,
	    test_cbi_gpio_before_after, test_cbi_gpio_before_after, NULL);
