/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

#define CHARGER_NUM get_charger_num(&isl923x_drv)
#define ISL923X_EMUL EMUL_DT_GET(DT_NODELABEL(isl923x_emul))
#define COMMON_DATA emul_isl923x_get_i2c_common_data(ISL923X_EMUL)

ZTEST_SUITE(init_prochot, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(init_prochot, test_init_dc_prochot)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	uint16_t val;

	/* Test failed CTRL2 register read (prochot debounce) */
	isl923x_emul_reset_registers(isl923x_emul);

	isl923x_drv.init(CHARGER_NUM);

	val = isl923x_emul_peek_reg(isl923x_emul, ISL923X_REG_PROCHOT_DC);

	if (CONFIG_CHARGER_DC_PROCHOT_CURRENT_MA == -1) {
		zassert_equal(val, ISL923X_EMUL_DC_PROCHOT_POR,
			      "DC PROCHOT not set to power on reset default");
	} else {
		zassert_not_equal(
			val, ISL923X_EMUL_DC_PROCHOT_POR,
			"DC PROCHOT still set to power on reset default");
	}

	if (CONFIG_CHARGER_DC_PROCHOT_CURRENT_MA != -1) {
		/* Verify error path */
		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   ISL923X_REG_PROCHOT_DC);

		isl923x_drv.init(CHARGER_NUM);
		val = isl923x_emul_peek_reg(isl923x_emul,
					    ISL923X_REG_PROCHOT_DC);

		zassert_equal(
			val, ISL923X_EMUL_DC_PROCHOT_POR,
			"DC PROCHOT not set to power on reset after error");

		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}
}

ZTEST(init_prochot, test_init_ac_prochot)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	uint16_t val;

	/* Test failed CTRL2 register read (prochot debounce) */
	isl923x_emul_reset_registers(isl923x_emul);

	isl923x_drv.init(CHARGER_NUM);

	val = isl923x_emul_peek_reg(isl923x_emul, ISL923X_REG_PROCHOT_AC);

	if (CONFIG_CHARGER_AC_PROCHOT_CURRENT_MA == -1) {
		zassert_equal(val, ISL923X_EMUL_AC_PROCHOT_POR,
			      "AC PROCHOT not set to power on reset default");
	} else {
		zassert_not_equal(
			val, ISL923X_EMUL_AC_PROCHOT_POR,
			"AC PROCHOT still set to power on reset default");
	}

	if (CONFIG_CHARGER_AC_PROCHOT_CURRENT_MA != -1) {
		/* Verify error path */
		isl923x_emul_reset_registers(isl923x_emul);
		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   ISL923X_REG_PROCHOT_AC);

		isl923x_drv.init(CHARGER_NUM);
		val = isl923x_emul_peek_reg(isl923x_emul,
					    ISL923X_REG_PROCHOT_AC);

		zassert_equal(
			val, ISL923X_EMUL_AC_PROCHOT_POR,
			"AC PROCHOT not set to power on reset after error");

		i2c_common_emul_set_write_fail_reg(COMMON_DATA,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}
}
