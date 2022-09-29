/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include "util.h"
#include "test/drivers/test_state.h"

ZTEST(util, reverse)
{
	uint8_t input[] = { 0, 1, 2, 3, 4 };
	uint8_t expected[] = { 4, 3, 2, 1, 0 };

	reverse(input, sizeof(input));

	zassert_mem_equal(input, expected, sizeof(input), NULL);
}

ZTEST(util, parse_offset_size__normal)
{
	const char *argv[] = { "cmd", "123", "456" };
	int argc = ARRAY_SIZE(argv);
	int offset = 0, size = 0;

	/* shift=1 is the position of the "123" CLI arg */
	zassert_ok(parse_offset_size(argc, argv, 1, &offset, &size), NULL);
	zassert_equal(123, offset, NULL);
	zassert_equal(456, size, NULL);
}

ZTEST(util, parse_offset_size__invalid_param1)
{
	const char *argv[] = { "cmd", "xyz" /* <- bad */, "456" };
	int argc = ARRAY_SIZE(argv);
	int offset = 0, size = 0;

	/* shift=1 is the position of the "123" CLI arg */
	zassert_equal(EC_ERROR_PARAM1,
		      parse_offset_size(argc, argv, 1, &offset, &size), NULL);
}

ZTEST(util, parse_offset_size__invalid_param2)
{
	const char *argv[] = { "cmd", "123", "xyz" /* <- bad */ };
	int argc = ARRAY_SIZE(argv);
	int offset = 0, size = 0;

	/* shift=1 is the position of the "123" CLI arg */
	zassert_equal(EC_ERROR_PARAM2,
		      parse_offset_size(argc, argv, 1, &offset, &size), NULL);
}

ZTEST(util, wait_for_ready)
{
	uint32_t reg;

	/* These calls should immediately exit. If not, the test will fail by
	 * virtue of timing out.
	 */
	reg = 1;
	wait_for_ready(&reg, 0, 1);

	reg = 0;
	wait_for_ready(&reg, 1, 1);
}

ZTEST(util, binary_from_bits)
{
	int input[] = {
		0,
		1,
		0,
		1,
	};

	zassert_equal(0xA, binary_from_bits(input, ARRAY_SIZE(input)), NULL);
	zassert_equal(0, binary_from_bits(NULL, 0), NULL);
}

ZTEST(util, ternary_from_bits)
{
	int input[] = {
		0,
		1,
		2,
		3,
	};

	/* Base 3 digits: 0*(3^0) + 1*(3^1) + 2*(3^2) + 3*(3^3) = 102 */

	zassert_equal(102, ternary_from_bits(input, ARRAY_SIZE(input)), NULL);
	zassert_equal(0, ternary_from_bits(NULL, 0), NULL);
}

ZTEST_SUITE(util, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
