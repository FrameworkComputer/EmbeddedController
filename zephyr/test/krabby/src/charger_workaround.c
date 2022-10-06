/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "charger.h"
#include "driver/charger/rt9490.h"
#include "emul/emul_rt9490.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"

FAKE_VALUE_FUNC(int, board_get_version);

const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(charger));

static bool ibus_adc_workaround_called(void)
{
	return rt9490_emul_peek_reg(emul, 0x52) == 0xC4;
}

static bool i2c_speed_workaround_called(void)
{
	return rt9490_emul_peek_reg(emul, 0x71) == 0x22;
}

static bool eoc_deglitch_workaround_called(void)
{
	return !(rt9490_emul_peek_reg(emul, RT9490_REG_ADD_CTRL0) &
		 RT9490_TD_EOC);
}

static bool disable_safety_timer_called(void)
{
	return rt9490_emul_peek_reg(emul, RT9490_REG_SAFETY_TMR_CTRL) ==
	       (RT9490_EN_TRICHG_TMR | RT9490_EN_PRECHG_TMR |
		RT9490_EN_FASTCHG_TMR);
}

ZTEST(charger_workaround, test_board_version_0)
{
	board_get_version_fake.return_val = 0;

	hook_notify(HOOK_INIT);
	zassert_true(ibus_adc_workaround_called(), NULL);
	zassert_true(i2c_speed_workaround_called(), NULL);
	zassert_false(eoc_deglitch_workaround_called(), NULL);
	zassert_true(disable_safety_timer_called(), NULL);
}

ZTEST(charger_workaround, test_board_version_1)
{
	board_get_version_fake.return_val = 1;

	hook_notify(HOOK_INIT);
	zassert_false(ibus_adc_workaround_called(), NULL);
	zassert_true(i2c_speed_workaround_called(), NULL);
	zassert_true(eoc_deglitch_workaround_called(), NULL);
	zassert_true(disable_safety_timer_called(), NULL);
}

ZTEST(charger_workaround, test_board_version_2)
{
	board_get_version_fake.return_val = 2;

	hook_notify(HOOK_INIT);
	zassert_false(ibus_adc_workaround_called(), NULL);
	zassert_true(i2c_speed_workaround_called(), NULL);
	zassert_false(eoc_deglitch_workaround_called(), NULL);
	zassert_false(disable_safety_timer_called(), NULL);
}

ZTEST(charger_workaround, test_board_version_3)
{
	board_get_version_fake.return_val = 3;

	hook_notify(HOOK_INIT);
	zassert_false(ibus_adc_workaround_called(), NULL);
	zassert_false(i2c_speed_workaround_called(), NULL);
	zassert_false(eoc_deglitch_workaround_called(), NULL);
	zassert_false(disable_safety_timer_called(), NULL);
}

static void charge_workaround_before(void *fixture)
{
	RESET_FAKE(board_get_version);
	rt9490_emul_reset_regs(emul);
}

ZTEST_SUITE(charger_workaround, NULL, NULL, charge_workaround_before, NULL,
	    NULL);
