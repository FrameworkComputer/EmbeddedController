/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "test/drivers/test_state.h"
#include "util.h"

#include <zephyr/ztest.h>

/*
 * TODO(b/254924012): Test alternative GPIO when emulated gpio tristate is
 * supported upstream.
 */

ZTEST(gpio_id, test_board_get_sku_id__default_value)
{
	int bits[] = {
		/* Default Hi-Z value */
		2,
	};

	zassert_equal(board_get_sku_id(),
		      ternary_from_bits(bits, ARRAY_SIZE(bits)));
}

ZTEST(gpio_id, test_board_get_version__default_value)
{
	int bits[] = {
		/* Default Hi-Z value */
		2,
	};

	zassert_equal(board_get_version(),
		      ternary_from_bits(bits, ARRAY_SIZE(bits)));
}

ZTEST_SUITE(gpio_id, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
