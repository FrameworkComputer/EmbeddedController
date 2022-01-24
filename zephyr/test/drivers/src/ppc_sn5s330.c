/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/emul.h>
#include <ztest.h>
#include <fff.h>

#include "driver/ppc/sn5s330.h"
#include "driver/ppc/sn5s330_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sn5s330.h"
#include "usbc_ppc.h"

/** This must match the index of the sn5s330 in ppc_chips[] */
#define SN5S330_PORT 0
#define EMUL emul_get_binding(DT_LABEL(DT_NODELABEL(sn5s330_emul)))
#define FUNC_SET1_ILIMPP1_MSK 0x1F
#define SN5S330_INTERRUPT_DELAYMS 15

FAKE_VOID_FUNC(sn5s330_emul_interrupt_set_stub);

/*
 * TODO(b/203364783): Exclude other threads from interacting with the emulator
 * to avoid test flakiness
 */

struct intercept_write_data {
	int reg_to_intercept;
	uint8_t val_intercepted;
};

struct intercept_read_data {
	int reg_to_intercept;
	bool replace_reg_val;
	uint8_t replacement_val;
};

static int intercept_read_func(struct i2c_emul *emul, int reg, uint8_t *val,
			       int bytes, void *data)
{
	struct intercept_read_data *test_data = data;

	if (test_data->reg_to_intercept && test_data->replace_reg_val)
		*val = test_data->replacement_val;

	return EC_SUCCESS;
}

static int intercept_write_func(struct i2c_emul *emul, int reg, uint8_t val,
				int bytes, void *data)
{
	struct intercept_write_data *test_data = data;

	if (test_data->reg_to_intercept == reg)
		test_data->val_intercepted = val;

	return 1;
}

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
	uint8_t func_set1_value;

	i2c_common_emul_set_write_func(i2c_emul, fail_until_write_func, &count);

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	zassert_equal(count, 0, NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_value);
	zassert_true((func_set1_value & SN5S330_ILIM_1_62) != 0, NULL);
	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
}

static void test_dead_battery_boot_force_pp2_fets_set(void)
{
	const struct emul *emul = EMUL;
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(emul);
	struct intercept_write_data test_write_data = {
		.reg_to_intercept = SN5S330_FUNC_SET3,
		.val_intercepted = 0,
	};
	struct intercept_read_data test_read_data = {
		.reg_to_intercept = SN5S330_INT_STATUS_REG4,
		.replace_reg_val = true,
		.replacement_val = SN5S330_DB_BOOT,
	};

	i2c_common_emul_set_write_func(i2c_emul, intercept_write_func,
				       &test_write_data);
	i2c_common_emul_set_read_func(i2c_emul, intercept_read_func,
				      &test_read_data);

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/*
	 * Although the device enables PP2_FET on dead battery boot by setting
	 * the PP2_EN bit, the driver also force sets this bit during dead
	 * battery boot by writing that bit to the FUNC_SET3 reg.
	 *
	 * TODO(b/207034759): Verify need or remove redundant PP2 set.
	 */
	zassert_true(test_write_data.val_intercepted & SN5S330_PP2_EN, NULL);
	zassert_false(sn5s330_drv.is_sourcing_vbus(SN5S330_PORT), NULL);
}

static void test_enter_low_power_mode(void)
{
	const struct emul *emul = EMUL;

	uint8_t func_set2_reg;
	uint8_t func_set3_reg;
	uint8_t func_set4_reg;
	uint8_t func_set9_reg;

	/*
	 * Requirements were extracted from TI's recommended changes for octopus
	 * to lower power use during hibernate as well as the follow up changes
	 * we made to allow the device to wake up from hibernate.
	 *
	 * For Reference: b/111006203#comment35
	 */

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	zassert_ok(sn5s330_drv.enter_low_power_mode(SN5S330_PORT), NULL);

	/* 1) Verify VBUS power paths are off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_PP1_EN, 0, NULL);
	zassert_equal(func_set3_reg & SN5S330_PP2_EN, 0, NULL);

	/* 2) Verify VCONN power path is off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET4, &func_set4_reg);
	zassert_not_equal(func_set4_reg & SN5S330_CC_EN, 0, NULL);
	zassert_equal(func_set4_reg & SN5S330_VCONN_EN, 0, NULL);

	/* 3) Verify SBU FET is off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET2, &func_set2_reg);
	zassert_equal(func_set2_reg & SN5S330_SBU_EN, 0, NULL);

	/* 4) Verify VBUS and SBU OVP comparators are off */
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET9, &func_set9_reg);
	zassert_equal(func_set9_reg & SN5S330_FORCE_OVP_EN_SBU, 0, NULL);
	zassert_equal(func_set9_reg & SN5S330_PWR_OVR_VBUS, 0, NULL);
	zassert_not_equal(func_set9_reg & SN5S330_OVP_EN_CC, 0, NULL);
	zassert_equal(func_set9_reg & SN5S330_FORCE_ON_VBUS_OVP, 0, NULL);
	zassert_equal(func_set9_reg & SN5S330_FORCE_ON_VBUS_UVP, 0, NULL);
}

static void test_vbus_source_sink_enable(void)
{
	const struct emul *emul = EMUL;
	uint8_t func_set3_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* Test enable/disable VBUS source FET */
	zassert_ok(sn5s330_drv.vbus_source_enable(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_not_equal(func_set3_reg & SN5S330_PP1_EN, 0, NULL);

	zassert_ok(sn5s330_drv.vbus_source_enable(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_PP1_EN, 0, NULL);

	/* Test enable/disable VBUS sink FET */
	zassert_ok(sn5s330_drv.vbus_sink_enable(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_not_equal(func_set3_reg & SN5S330_PP2_EN, 0, NULL);

	zassert_ok(sn5s330_drv.vbus_sink_enable(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_PP2_EN, 0, NULL);
}

static void test_vbus_discharge(void)
{
	const struct emul *emul = EMUL;
	uint8_t func_set3_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* Test enable/disable VBUS discharging */
	zassert_ok(sn5s330_drv.discharge_vbus(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_not_equal(func_set3_reg & SN5S330_VBUS_DISCH_EN, 0, NULL);

	zassert_ok(sn5s330_drv.discharge_vbus(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET3, &func_set3_reg);
	zassert_equal(func_set3_reg & SN5S330_VBUS_DISCH_EN, 0, NULL);
}

static void test_set_vbus_source_current_limit(void)
{
	const struct emul *emul = EMUL;
	uint8_t func_set1_reg;

	/* Test every TCPC Pull Resistance Value */
	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* USB */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_USB),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_0_63,
		      NULL);

	/* 1.5A */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_1A5),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_1_62,
		      NULL);

	/* 3.0A */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_3A0),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_3_06,
		      NULL);

	/* Unknown/Reserved - We set result as USB */
	zassert_ok(sn5s330_drv.set_vbus_source_current_limit(SN5S330_PORT,
							     TYPEC_RP_RESERVED),
		   NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET1, &func_set1_reg);
	zassert_equal(func_set1_reg & FUNC_SET1_ILIMPP1_MSK, SN5S330_ILIM_0_63,
		      NULL);
}

static void test_sn5s330_set_sbu(void)
#ifdef CONFIG_USBC_PPC_SBU
{
	const struct emul *emul = EMUL;
	uint8_t func_set2_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	/* Verify driver enables SBU FET */
	zassert_ok(sn5s330_drv.set_sbu(SN5S330_PORT, true), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET2, &func_set2_reg);
	zassert_not_equal(func_set2_reg & SN5S330_SBU_EN, 0, NULL);

	/* Verify driver disables SBU FET */
	zassert_ok(sn5s330_drv.set_sbu(SN5S330_PORT, false), NULL);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET2, &func_set2_reg);
	zassert_equal(func_set2_reg & SN5S330_SBU_EN, 0, NULL);
}
#else
{
	ztest_test_skip();
}
#endif /* CONFIG_USBC_PPC_SBU */

static void test_sn5s330_vbus_overcurrent(void)
{
	const struct emul *emul = EMUL;
	uint8_t int_trip_rise_reg1;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	sn5s330_emul_make_vbus_overcurrent(emul);
	/*
	 * TODO(b/201420132): Replace arbitrary sleeps.
	 */
	/* Make sure interrupt happens first. */
	k_msleep(SN5S330_INTERRUPT_DELAYMS);
	zassert_true(sn5s330_emul_interrupt_set_stub_fake.call_count > 0, NULL);

	/*
	 * Verify we cleared vbus overcurrent interrupt trip rise bit so the
	 * driver can detect future overcurrent clamping interrupts.
	 */
	sn5s330_emul_peek_reg(emul, SN5S330_INT_TRIP_RISE_REG1,
			      &int_trip_rise_reg1);
	zassert_equal(int_trip_rise_reg1 & SN5S330_ILIM_PP1_MASK, 0, NULL);
}

static void test_sn5s330_disable_vbus_low_interrupt(void)
#ifdef CONFIG_USBC_PPC_VCONN
{
	const struct emul *emul = EMUL;

	/* Interrupt disabled here */
	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);
	/* Would normally cause a vbus low interrupt */
	sn5s330_emul_lower_vbus_below_minv(emul);
	zassert_equal(sn5s330_emul_interrupt_set_stub_fake.call_count, 0, NULL);
}
#else
{
	ztest_test_skip();
}
#endif /* CONFIG_USBC_PPC_VCONN */

static void test_sn5s330_set_vconn_fet(void)
{
	const struct emul *emul = EMUL;
	uint8_t func_set4_reg;

	zassert_ok(sn5s330_drv.init(SN5S330_PORT), NULL);

	sn5s330_drv.set_vconn(SN5S330_PORT, false);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET4, &func_set4_reg);
	zassert_equal(func_set4_reg & SN5S330_VCONN_EN, 0, NULL);

	sn5s330_drv.set_vconn(SN5S330_PORT, true);
	sn5s330_emul_peek_reg(emul, SN5S330_FUNC_SET4, &func_set4_reg);
	zassert_not_equal(func_set4_reg & SN5S330_VCONN_EN, 0, NULL);
}

static void reset_sn5s330_state(void)
{
	struct i2c_emul *i2c_emul = sn5s330_emul_to_i2c_emul(EMUL);

	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
	sn5s330_emul_reset(EMUL);
	RESET_FAKE(sn5s330_emul_interrupt_set_stub);
}

void test_suite_ppc_sn5s330(void)
{
	ztest_test_suite(
		ppc_sn5s330,
		ztest_unit_test_setup_teardown(test_sn5s330_set_vconn_fet,
					       reset_sn5s330_state,
					       reset_sn5s330_state),
		ztest_unit_test_setup_teardown(
			test_sn5s330_disable_vbus_low_interrupt,
			reset_sn5s330_state, reset_sn5s330_state),
		ztest_unit_test_setup_teardown(test_sn5s330_vbus_overcurrent,
					       reset_sn5s330_state,
					       reset_sn5s330_state),
		ztest_unit_test_setup_teardown(test_sn5s330_set_sbu,
					       reset_sn5s330_state,
					       reset_sn5s330_state),
		ztest_unit_test_setup_teardown(
			test_set_vbus_source_current_limit, reset_sn5s330_state,
			reset_sn5s330_state),
		ztest_unit_test_setup_teardown(test_vbus_discharge,
					       reset_sn5s330_state,
					       reset_sn5s330_state),
		ztest_unit_test_setup_teardown(test_vbus_source_sink_enable,
					       reset_sn5s330_state,
					       reset_sn5s330_state),
		ztest_unit_test_setup_teardown(test_enter_low_power_mode,
					       reset_sn5s330_state,
					       reset_sn5s330_state),
		ztest_unit_test_setup_teardown(
			test_dead_battery_boot_force_pp2_fets_set,
			reset_sn5s330_state, reset_sn5s330_state),
		ztest_unit_test_setup_teardown(test_fail_once_func_set1,
					       reset_sn5s330_state,
					       reset_sn5s330_state));
	ztest_run_test_suite(ppc_sn5s330);
}
