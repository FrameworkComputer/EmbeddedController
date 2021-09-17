/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>
#include "driver/accel_lis2dw12.h"
#include "emul/emul_lis2dw12.h"

#define LIS2DW12_NODELABEL DT_NODELABEL(ms_lis2dw12_accel)
#define LIS2DW12_SENSOR_ID SENSOR_ID(LIS2DW12_NODELABEL)
#define EMUL_LABEL DT_LABEL(DT_NODELABEL(lis2dw12_emul))

#include <stdio.h>
static void lis2dw12_setup(void)
{
	lis2dw12_emul_reset(emul_get_binding(EMUL_LABEL));
}

static void test_lis2dw12_init__fail_who_am_i(void)
{
	const struct emul *emul = emul_get_binding(EMUL_LABEL);
	struct motion_sensor_t *ms = &motion_sensors[LIS2DW12_SENSOR_ID];
	int rv;

	lis2dw12_emul_set_who_am_i(emul, ~LIS2DW12_WHO_AM_I);

	rv = ms->drv->init(ms);
	zassert_equal(EC_ERROR_ACCESS_DENIED, rv,
		      "init returned %d but was expecting %d", rv,
		      EC_ERROR_ACCESS_DENIED);
}

void test_suite_lis2dw12(void)
{
	ztest_test_suite(lis2dw12, ztest_unit_test_setup_teardown(
					   test_lis2dw12_init__fail_who_am_i,
					   lis2dw12_setup, unit_test_noop));
	ztest_run_test_suite(lis2dw12);
}
