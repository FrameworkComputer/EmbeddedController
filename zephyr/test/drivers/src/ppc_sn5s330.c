/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <devicetree.h>
#include <emul.h>
#include <ztest.h>

#include "driver/ppc/sn5s330.h"
#include "driver/ppc/sn5s330_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sn5s330.h"
#include "usbc_ppc.h"

/** This must match the index of the sn5s330 in ppc_chips[] */
#define SN5S330_PORT 0
#define EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(sn5s330_emul)))

/*
 * TODO(b/203364783): Exclude other threads from interacting with the emulator
 * to avoid test flakiness
 */

static int fail_until_write_func(struct i2c_emul *emul, int reg, uint8_t val,
				 int bytes, void *data)
{
	uint32_t *count = data;

	if (*count != 0) {
		*count -= 1;
		return -EIO;
	}
	return 1;
}

static void test_fail_once_func_set1(void)
{
	const struct emul *emul = EMUL;
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(emul);
	uint32_t count = 1;
	uint32_t func_set1_value;

	i2c_common_emul_set_write_func(i2c_emul, fail_until_write_func, &count);

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	zassert_equal(count, 0, NULL);
	zassert_ok(sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1,
					 &func_set1_value),
		   NULL);
	zassert_true((func_set1_value & SN5S330_ILIM_1_62) != 0, NULL);
	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
}

void test_suite_ppc_sn5s330(void)
{
	ztest_test_suite(
		ppc_sn5s330,
		ztest_unit_test(test_fail_once_func_set1));
	ztest_run_test_suite(ppc_sn5s330);
}
