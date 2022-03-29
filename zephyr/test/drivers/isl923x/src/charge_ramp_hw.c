/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include <ztest.h>

#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_state.h"

#define CHARGER_NUM get_charger_num(&isl923x_drv)
#define ISL923X_EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)))

ZTEST_SUITE(charge_ramp_hw, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST(charge_ramp_hw, test_charge_ramp_hw_ramp)
{
	zassert_ok(isl923x_drv.set_hw_ramp(CHARGER_NUM, 1), NULL);

	zassert_ok(isl923x_drv.ramp_is_stable(CHARGER_NUM), NULL);
	zassert_true(isl923x_drv.ramp_is_detected(CHARGER_NUM), NULL);

	zassert_ok(isl923x_drv.set_input_current_limit(CHARGER_NUM, 512), NULL);
	zassert_equal(512, isl923x_drv.ramp_get_current_limit(CHARGER_NUM),
		      NULL);
}

ZTEST(charge_ramp_hw, test_charge_ramp_hw_ramp_read_fail_reg0)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);

	i2c_common_emul_set_read_fail_reg(i2c_emul, ISL923X_REG_CONTROL0);
	zassert_equal(EC_ERROR_INVAL, isl923x_drv.set_hw_ramp(CHARGER_NUM, 1),
		      NULL);
}

ZTEST(charge_ramp_hw, test_charge_ramp_hw_ramp_read_fail_acl1)
{
	const struct emul *isl923x_emul = ISL923X_EMUL;
	struct i2c_emul *i2c_emul = isl923x_emul_get_i2c_emul(isl923x_emul);

	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  ISL923X_REG_ADAPTER_CURRENT_LIMIT1);
	zassert_equal(0, isl923x_drv.ramp_get_current_limit(CHARGER_NUM), NULL);
}
