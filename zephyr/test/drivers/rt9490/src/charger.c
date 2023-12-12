/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "charger.h"
#include "driver/charger/rt9490.h"
#include "emul/emul_rt9490.h"
#include "i2c.h"

#include <zephyr/ztest.h>

static const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(rt9490));
static const int chgnum = CHARGER_SOLO;

ZTEST(rt9490_chg, test_current)
{
	struct {
		int reg;
		int expected; /* expected current in mA */
	} testdata[] = { { 0xF, 150 },	 { 0x10, 160 },	  { 0x64, 1000 },
			 { 0xC8, 2000 }, { 0x1F3, 4990 }, { 0x1F4, 5000 } };

	for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
		int current = -1;

		zassert_ok(rt9490_drv.set_current(chgnum, testdata[i].expected),
			   "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_ICHG_CTRL),
			      testdata[i].reg >> 8, "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul,
						   RT9490_REG_ICHG_CTRL + 1),
			      testdata[i].reg & 0xFF, "case %d failed", i);

		zassert_ok(rt9490_drv.get_current(chgnum, &current),
			   "case %d failed", i);
		zassert_equal(testdata[i].expected, current, "case %d failed",
			      i);
	}

	/* special case: set_current(0) means 150mA */
	zassert_ok(rt9490_drv.set_current(chgnum, 0), NULL);
	zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_ICHG_CTRL), 0,
		      NULL);
	zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_ICHG_CTRL + 1), 0xF,
		      NULL);

	/* values outside (150mA, 5000mA) are illegal */
	zassert_not_equal(rt9490_drv.set_current(chgnum, 140), 0, NULL);
	zassert_not_equal(rt9490_drv.set_current(chgnum, 5001), 0, NULL);
}

ZTEST(rt9490_chg, test_voltage)
{
	struct {
		int reg;
		int expected; /* expected voltage in mV */
	} testdata[] = { { 0x12C, 3000 },  { 0x12D, 3010 },  { 0x12E, 3020 },
			 { 0x1A4, 4200 },  { 0x348, 8400 },  { 0x4EC, 12600 },
			 { 0x690, 16800 }, { 0x757, 18790 }, { 0x758, 18800 } };

	for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
		int voltage = -1;

		zassert_ok(rt9490_drv.set_voltage(chgnum, testdata[i].expected),
			   "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_VCHG_CTRL),
			      testdata[i].reg >> 8, "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul,
						   RT9490_REG_VCHG_CTRL + 1),
			      testdata[i].reg & 0xFF, "case %d failed", i);

		zassert_ok(rt9490_drv.get_voltage(chgnum, &voltage),
			   "case %d failed", i);
		zassert_equal(testdata[i].expected, voltage, "case %d failed",
			      i);
	}

	/* special case: set_voltage(0) means 3.0V */
	zassert_ok(rt9490_drv.set_voltage(chgnum, 0), NULL);
	zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_VCHG_CTRL), 0x1,
		      NULL);
	zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_VCHG_CTRL + 1),
		      0x2C, NULL);

	/* values outside (3V, 18.8V) are illegal */
	zassert_not_equal(rt9490_drv.set_voltage(chgnum, 2999), 0, NULL);
	zassert_not_equal(rt9490_drv.set_voltage(chgnum, 18801), 0, NULL);
}

ZTEST(rt9490_chg, test_otg)
{
	struct {
		int reg_v, reg_c, expected_v, expected_c;
	} testdata[] = { { 0x0, 0x3, 2800, 120 },
			 { 0x1, 0x4, 2810, 160 },
			 { 0xDC, 0x4B, 5000, 3000 },
			 { 0x77F, 0x52, 21990, 3280 },
			 { 0x780, 0x53, 22000, 3320 } };

	for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
		zassert_ok(rt9490_drv.set_otg_current_voltage(
				   chgnum, testdata[i].expected_c,
				   testdata[i].expected_v),
			   "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_VOTG_REGU),
			      testdata[i].reg_v >> 8, "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul,
						   RT9490_REG_VOTG_REGU + 1),
			      testdata[i].reg_v & 0xFF, "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_IOTG_REGU),
			      testdata[i].reg_c, "case %d failed", i);
		break;
	}

	/* check out-of-range inputs */
	zassert_not_equal(rt9490_drv.set_otg_current_voltage(chgnum, 119, 5000),
			  0, NULL);
	zassert_not_equal(rt9490_drv.set_otg_current_voltage(chgnum, 3330,
							     5000),
			  0, NULL);
	zassert_not_equal(rt9490_drv.set_otg_current_voltage(chgnum, 3000,
							     2700),
			  0, NULL);
	zassert_not_equal(rt9490_drv.set_otg_current_voltage(chgnum, 3000,
							     23000),
			  0, NULL);

	/* check enable/disable functions */
	zassert_equal(rt9490_drv.enable_otg_power(chgnum, true), 0, NULL);
	zassert_equal(rt9490_drv.is_sourcing_otg_power(chgnum, 0), true, NULL);
	zassert_equal(rt9490_drv.enable_otg_power(chgnum, false), 0, NULL);
	zassert_equal(rt9490_drv.is_sourcing_otg_power(chgnum, 0), false, NULL);
}

ZTEST(rt9490_chg, test_aicr)
{
	struct {
		int reg;
		int expected; /* expected current in mA */
	} testdata[] = { { 0xA, 100 },
			 { 0xB, 110 },
			 { 0x32, 500 },
			 { 0x12C, 3000 },
			 { 0x14A, 3300 } };
	int current = -1;

	for (int i = 0; i < ARRAY_SIZE(testdata); i++) {
		zassert_ok(rt9490_drv.set_input_current_limit(

				   chgnum, testdata[i].expected),
			   "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul, RT9490_REG_AICR_CTRL),
			      testdata[i].reg >> 8, "case %d failed", i);
		zassert_equal(rt9490_emul_peek_reg(emul,
						   RT9490_REG_AICR_CTRL + 1),
			      testdata[i].reg & 0xFF, "case %d failed", i);

		zassert_ok(rt9490_drv.get_input_current_limit(chgnum, &current),
			   "case %d failed", i);
		zassert_equal(testdata[i].expected, current, "case %d failed",
			      i);
	}

	/*
	 * test values outside the designed range.
	 * returns 100mA if input < 100mA, and 3300mA if greater than 3300mA.
	 */
	zassert_ok(rt9490_drv.set_input_current_limit(chgnum, 90), NULL);
	zassert_ok(rt9490_drv.get_input_current_limit(chgnum, &current), "");
	zassert_equal(100, current, "");

	zassert_ok(rt9490_drv.set_input_current_limit(chgnum, 3400), NULL);
	zassert_ok(rt9490_drv.get_input_current_limit(chgnum, &current), "");
	zassert_equal(3300, current, "");
}

ZTEST(rt9490_chg, test_charge_ramp_hw_ramp)
{
	zassert_ok(rt9490_drv.set_hw_ramp(chgnum, 1), NULL);
	zassert_true(rt9490_emul_peek_reg(emul, RT9490_REG_CHG_CTRL0) &
			     RT9490_EN_AICC,
		     NULL);

	zassert_ok(rt9490_drv.ramp_is_stable(chgnum), NULL);
	zassert_true(rt9490_drv.ramp_is_detected(chgnum), NULL);

	zassert_ok(rt9490_drv.set_input_current_limit(chgnum, 500), NULL);
	zassert_equal(500, rt9490_drv.ramp_get_current_limit(chgnum), NULL);

	zassert_ok(rt9490_drv.set_hw_ramp(chgnum, 0), NULL);
	zassert_false(rt9490_emul_peek_reg(emul, RT9490_REG_CHG_CTRL0) &
			      RT9490_EN_AICC,
		      NULL);
}

ZTEST(rt9490_chg, test_option)
{
	int opt;

	zassert_ok(rt9490_drv.get_option(CHARGER_NUM, &opt), NULL);
	zassert_true(opt == 0, NULL);
	zassert_ok(rt9490_drv.set_option(CHARGER_NUM, 5566), NULL);
	zassert_ok(rt9490_drv.get_option(CHARGER_NUM, &opt), NULL);
	zassert_true(opt == 0, NULL);
}

ZTEST(rt9490_chg, test_misc_info)
{
	int status;
	int device_id;

	rt9490_drv.dump_registers(chgnum);

	zassert_ok(rt9490_drv.device_id(chgnum, &device_id), NULL);
	zassert_equal((device_id >> 3) & 0xF, 0xC, NULL);

	zassert_ok(rt9490_drv.get_status(chgnum, &status), NULL);
	zassert_equal(status, 0, NULL);

	/* check the mapping from jeita status to smart battery charger status
	 */
	zassert_ok(rt9490_emul_write_reg(emul, RT9490_REG_CHG_STATUS4,
					 RT9490_JEITA_HOT_MASK));
	zassert_ok(rt9490_drv.get_status(chgnum, &status), NULL);
	zassert_equal(status, CHARGER_RES_HOT | CHARGER_RES_OR, NULL);

	zassert_ok(rt9490_emul_write_reg(emul, RT9490_REG_CHG_STATUS4,
					 RT9490_JEITA_WARM_MASK));
	zassert_ok(rt9490_drv.get_status(chgnum, &status), NULL);
	zassert_equal(status, CHARGER_RES_HOT, NULL);

	zassert_ok(rt9490_emul_write_reg(emul, RT9490_REG_CHG_STATUS4,
					 RT9490_JEITA_COOL_MASK));
	zassert_ok(rt9490_drv.get_status(chgnum, &status), NULL);
	zassert_equal(status, CHARGER_RES_COLD, NULL);

	zassert_ok(rt9490_emul_write_reg(emul, RT9490_REG_CHG_STATUS4,
					 RT9490_JEITA_COLD_MASK));
	zassert_ok(rt9490_drv.get_status(chgnum, &status), NULL);
	zassert_equal(status, CHARGER_RES_COLD | CHARGER_RES_UR, NULL);
}

static void rt9490_chg_setup(void)
{
	batt_conf_main();
}

static void reset_emul(void *fixture)
{
	rt9490_emul_reset_regs(emul);
	rt9490_drv.init(chgnum);
}

ZTEST_SUITE(rt9490_chg, NULL, rt9490_chg_setup, reset_emul, NULL, NULL);
