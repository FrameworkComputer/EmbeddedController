/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_tcpci.h"
#include "hooks.h"
#include "i2c.h"
#include "stubs.h"

#include "tcpm/tcpci.h"

#define EMUL_LABEL DT_NODELABEL(tcpci_emul)

/** Check TCPC register value */
static void check_tcpci_reg_f(const struct emul *emul, int reg,
			      uint16_t exp_val, int line)
{
	uint16_t reg_val;

	zassert_ok(tcpci_emul_get_reg(emul, reg, &reg_val),
		   "Failed tcpci_emul_get_reg(); line: %d", line);
	zassert_equal(exp_val, reg_val, "Expected 0x%x, got 0x%x; line: %d",
		      exp_val, reg_val, line);
}
#define check_tcpci_reg(emul, reg, exp_val)			\
	check_tcpci_reg_f((emul), (reg), (exp_val), __LINE__)

/** Test TCPCI init and vbus level */
static void test_tcpci_init(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint16_t exp_mask;

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0 &
					  TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Test fail on power status read */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_POWER_STATUS);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_init(USBC_PORT_C0), NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on uninitialised bit set */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT, tcpci_tcpm_init(USBC_PORT_C0), NULL);

	/*
	 * Set expected alert mask. It is used in test until vSafe0V tcpc
	 * config flag is revmoved.
	 */
	exp_mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		   TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		   TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS |
		   TCPC_REG_ALERT_FAULT | TCPC_REG_ALERT_POWER_STATUS;

	/* Set TCPCI emulator VBUS to safe0v (disconnected) */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS, 0);

	/* Test init with VBUS safe0v without vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C0), NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_SAFE0V),
		     NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_PRESENT),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Set TCPCI emulator VBUS to present (connected, above 4V) */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_PRES);

	/* Test init with VBUS present without vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C0), NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_SAFE0V),
		      NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_PRESENT),
		     NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Disable vSafe0V tcpc config flag and update expected alert mask */
	exp_mask |= TCPC_REG_ALERT_EXT_STATUS;
	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;

	/* Test init with VBUS present with vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C0), NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_SAFE0V),
		      NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_PRESENT),
		     NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Set TCPCI emulator VBUS to safe0v (disconnected) */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS, 0);
	tcpci_emul_set_reg(emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	/* Test init with VBUS safe0v with vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C0), NULL);
	zassert_true(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_SAFE0V),
		      NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_PRESENT),
		     NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/*
	 * Set TCPCI emulator VBUS to disconnected but not at vSafe0V
	 * (VBUS in 0.8V - 3.5V range)
	 */
	tcpci_emul_set_reg(emul, TCPC_REG_EXT_STATUS, 0);

	/* Test init with VBUS not safe0v with vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_init(USBC_PORT_C0), NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_SAFE0V),
		      NULL);
	zassert_false(tcpci_tcpm_check_vbus_level(USBC_PORT_C0, VBUS_PRESENT),
		     NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);
}

/** Test TCPCI release */
static void test_tcpci_release(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0xffff);

	zassert_equal(EC_SUCCESS, tcpci_tcpm_release(USBC_PORT_C0), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0);
}

/** Test TCPCI get cc */
static void test_tcpci_get_cc(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	enum tcpc_cc_voltage_status cc1, cc2;
	uint16_t cc_status, role_ctrl;

	struct {
		/* TCPCI CC status register */
		enum tcpc_cc_voltage_status cc[2];
		bool connect_result;
		/* TCPCI ROLE ctrl register */
		enum tcpc_cc_pull role_cc[2];
		enum tcpc_drp drp;
	} test_param[] = {
		/* Test DRP with open state */
		{
			.cc = {TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN},
			.connect_result = false,
			.drp = TYPEC_DRP,
		},
		/* Test DRP with cc1 open state, cc2 src RA */
		{
			.cc = {TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RA},
			.connect_result = false,
			.drp = TYPEC_DRP,
		},
		/* Test DRP with cc1 src RA, cc2 src RD */
		{
			.cc = {TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_RD},
			.connect_result = false,
			.drp = TYPEC_DRP,
		},
		/* Test DRP with cc1 snk open, cc2 snk default */
		{
			.cc = {TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RP_DEF},
			.connect_result = true,
			.drp = TYPEC_DRP,
		},
		/* Test DRP with cc1 snk 1.5, cc2 snk 3.0 */
		{
			.cc = {TYPEC_CC_VOLT_RP_1_5, TYPEC_CC_VOLT_RP_3_0},
			.connect_result = true,
			.drp = TYPEC_DRP,
		},
		/* Test no DRP with cc1 src open, cc2 src RA */
		{
			.cc = {TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RA},
			.connect_result = false,
			.drp = TYPEC_NO_DRP,
			.role_cc = {TYPEC_CC_RP, TYPEC_CC_RP},
		},
		/* Test no DRP with cc1 src RD, cc2 snk default */
		{
			.cc = {TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RP_DEF},
			.connect_result = false,
			.drp = TYPEC_NO_DRP,
			.role_cc = {TYPEC_CC_RP, TYPEC_CC_RD},
		},
		/* Test no DRP with cc1 snk default, cc2 snk open */
		{
			.cc = {TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_OPEN},
			.connect_result = false,
			.drp = TYPEC_NO_DRP,
			.role_cc = {TYPEC_CC_RD, TYPEC_CC_RD},
		},
		/* Test no DRP with cc1 snk 3.0, cc2 snk 1.5 */
		{
			.cc = {TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_RP_1_5},
			.connect_result = false,
			.drp = TYPEC_NO_DRP,
			.role_cc = {TYPEC_CC_RD, TYPEC_CC_RD},
		},
	};

	for (int i = 0; i < ARRAY_SIZE(test_param); i++) {
		role_ctrl = TCPC_REG_ROLE_CTRL_SET(test_param[i].drp, 0,
						   test_param[i].role_cc[0],
						   test_param[i].role_cc[1]);
		/* If CC status is TYPEC_CC_VOLT_RP_*, then BIT(2) is ignored */
		cc_status = TCPC_REG_CC_STATUS_SET(test_param[i].connect_result,
						   test_param[i].cc[0],
						   test_param[i].cc[1]);
		tcpci_emul_set_reg(emul, TCPC_REG_ROLE_CTRL, role_ctrl);
		tcpci_emul_set_reg(emul, TCPC_REG_CC_STATUS, cc_status);
		zassert_equal(EC_SUCCESS,
			      tcpci_tcpm_get_cc(USBC_PORT_C0, &cc1, &cc2),
			      "Failed to get CC in test case %d (CC 0x%x, role 0x%x)",
			      i, cc_status, role_ctrl);
		zassert_equal(test_param[i].cc[0], cc1,
			      "0x%x != (cc1 = 0x%x) in test case %d (CC 0x%x, role 0x%x)",
			      test_param[i].cc[0], cc1, i, cc_status,
			      role_ctrl);
		zassert_equal(test_param[i].cc[1], cc2,
			      "0x%x != (cc2 = 0x%x) in test case %d (CC 0x%x, role 0x%x)",
			      test_param[i].cc[0], cc1, i, cc_status,
			      role_ctrl);
	}
}

/** Test TCPCI set cc */
static void test_tcpci_set_cc(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	enum tcpc_rp_value rp;
	enum tcpc_cc_pull cc;

	/* Test setting default RP and cc open */
	rp = TYPEC_RP_USB;
	cc = TYPEC_CC_OPEN;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_select_rp_value(USBC_PORT_C0, rp),
		      NULL);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_cc(USBC_PORT_C0, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));

	/* Test error on failed role ctrl set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_set_cc(USBC_PORT_C0, TYPEC_CC_OPEN), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting 1.5 RP and cc RD */
	rp = TYPEC_RP_1A5;
	cc = TYPEC_CC_RD;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_select_rp_value(USBC_PORT_C0, rp),
		      NULL);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_cc(USBC_PORT_C0, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));

	/* Test setting 3.0 RP and cc RP */
	rp = TYPEC_RP_3A0;
	cc = TYPEC_CC_RP;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_select_rp_value(USBC_PORT_C0, rp),
		      NULL);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_cc(USBC_PORT_C0, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));

	/*
	 * Test setting 3.0 RP and cc RA. tcpci_tcpm_select_rp_value() is
	 * intentionally not called to check if selected rp is persistent.
	 */
	cc = TYPEC_CC_RA;
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_cc(USBC_PORT_C0, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));
}

/** Test TCPCI set polarity */
static void test_tcpci_set_polarity(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint8_t initial_ctrl;
	uint8_t exp_ctrl;

	/* Set initial value for TCPC ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL |
		       TCPC_REG_TCPC_CTRL_BIST_TEST_MODE;
	tcpci_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, initial_ctrl);

	/* Test error on failed polarity set */
	exp_ctrl = initial_ctrl;
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_set_polarity(USBC_PORT_C0, POLARITY_CC2),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC2 */
	exp_ctrl = initial_ctrl | TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C0,
							  POLARITY_CC2), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC1 */
	exp_ctrl = initial_ctrl & ~TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C0,
							  POLARITY_CC1), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC2 DTS */
	exp_ctrl = initial_ctrl | TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C0,
							  POLARITY_CC2_DTS),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC1 DTS */
	exp_ctrl = initial_ctrl & ~TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_polarity(USBC_PORT_C0,
							  POLARITY_CC1_DTS),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
}

/** Test TCPCI set vconn */
static void test_tcpci_set_vconn(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint8_t initial_ctrl;
	uint8_t exp_ctrl;

	/* Set initial value for POWER ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
		       TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_CTRL, initial_ctrl);

	/* Test error on failed vconn set */
	exp_ctrl = initial_ctrl;
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_POWER_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_set_vconn(USBC_PORT_C0, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test vconn enable */
	exp_ctrl = initial_ctrl | TCPC_REG_POWER_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_vconn(USBC_PORT_C0, 1), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test vconn disable */
	exp_ctrl = initial_ctrl & ~TCPC_REG_POWER_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_vconn(USBC_PORT_C0, 0), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);
}

void test_suite_tcpci(void)
{
	ztest_test_suite(tcpci,
			 ztest_user_unit_test(test_tcpci_init),
			 ztest_user_unit_test(test_tcpci_release),
			 ztest_user_unit_test(test_tcpci_get_cc),
			 ztest_user_unit_test(test_tcpci_set_cc),
			 ztest_user_unit_test(test_tcpci_set_polarity),
			 ztest_user_unit_test(test_tcpci_set_vconn));
	ztest_run_test_suite(tcpci);
}
