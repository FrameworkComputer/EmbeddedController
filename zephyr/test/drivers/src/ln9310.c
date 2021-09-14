/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>
#include "driver/ln9310.h"
#include "emul/emul_ln9310.h"

void test_ln9310_2s_no_startup__passes_init(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
}

void test_ln9310_3s_no_startup__passes_init(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_3S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
}

void test_suite_ln9310(void)
{
	ztest_test_suite(
		ln9310,
		ztest_unit_test(test_ln9310_2s_no_startup__passes_init),
		ztest_unit_test(test_ln9310_3s_no_startup__passes_init));
	ztest_run_test_suite(ln9310);
}
