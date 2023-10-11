/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/stdio.h"
#include "common.h"
#include "math.h"
#include "math_util.h"

#include <zephyr/ztest.h>

ZTEST_USER(math, test_arc_cos__x_below_range)
{
	fp_t result = arc_cos(FLOAT_TO_FP(-1.1));

	zassert_within(result, FLOAT_TO_FP(180.0), FLOAT_TO_FP(1.0),
		       "arc_cos(-1.1) was %d", FP_TO_INT(result));
}

ZTEST_USER(math, test_arc_cos__x_above_range)
{
	fp_t result = arc_cos(FLOAT_TO_FP(1.1));

	zassert_within(result, FLOAT_TO_FP(0), FLOAT_TO_FP(1.0),
		       "arc_cos(1.1) was %d", FP_TO_INT(result));
}

ZTEST_USER(math, test_int_sqrtf)
{
	zassert_equal(int_sqrtf(0), 0);
	zassert_equal(int_sqrtf(15), 3);
	zassert_equal(int_sqrtf(25), 5);
	zassert_equal(int_sqrtf(11108889), 3333);
	zassert_equal(int_sqrtf(1234321), 1111);
}

ZTEST_USER(math, test_fp_sqrtf)
{
	zassert_within(fp_sqrtf(FLOAT_TO_FP(15)), FLOAT_TO_FP(3.872983),
		       FLOAT_TO_FP(0.001), NULL);
}

ZTEST_USER(math, test_print_ints)
{
	char buffer[10];

	/* Fixed point. */
	zassert_true(crec_snprintf(buffer, sizeof(buffer), "%.5d", 123) > 0,
		     NULL);
	zassert_equal(0, strcmp(buffer, "0.00123"), "got '%s'", buffer);
	zassert_true(crec_snprintf(buffer, sizeof(buffer), "%2.1d", 123) > 0,
		     NULL);
	zassert_equal(0, strcmp(buffer, "12.3"), "got '%s'", buffer);

	/* Precision or width larger than buffer should fail. */
	zassert_equal(-EC_ERROR_OVERFLOW, crec_snprintf(buffer, 4, "%5d", 123),
		      NULL);
	zassert_equal(0, strcmp(buffer, "  1"), "got '%s'", buffer);
	zassert_equal(-EC_ERROR_OVERFLOW, crec_snprintf(buffer, 4, "%10d", 123),
		      NULL);
	zassert_equal(0, strcmp(buffer, "   "), "got '%s'", buffer);
	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "%-10d", 123), NULL);
	zassert_equal(0, strcmp(buffer, "123"), "got '%s'", buffer);
	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "%.10d", 123), NULL);
	zassert_equal(0, strcmp(buffer, "0.0"), "got '%s'", buffer);
}
