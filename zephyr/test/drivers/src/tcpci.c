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

/** Test TCPCI set msg header */
static void test_tcpci_set_msg_header(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	/* Test error on failed header set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_MSG_HDR_INFO);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C0, PD_ROLE_SINK,
						PD_ROLE_UFP), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting sink UFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C0, PD_ROLE_SINK,
						PD_ROLE_UFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_UFP, PD_ROLE_SINK));

	/* Test setting sink DFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C0, PD_ROLE_SINK,
						PD_ROLE_DFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_DFP, PD_ROLE_SINK));

	/* Test setting source UFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C0, PD_ROLE_SOURCE,
						PD_ROLE_UFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_UFP, PD_ROLE_SOURCE));

	/* Test setting source DFP */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_set_msg_header(USBC_PORT_C0, PD_ROLE_SOURCE,
						PD_ROLE_DFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_DFP, PD_ROLE_SOURCE));
}

/** Test TCPCI rx and sop prime enable */
static void test_tcpci_set_rx_detect(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	/* Test error from rx_enable on rx detect set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_RX_DETECT);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_set_rx_enable(USBC_PORT_C0, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test rx disable */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C0, 0),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT, 0x0);

	/* Test setting sop prime with rx disable doesn't change RX_DETECT */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_sop_prime_enable(USBC_PORT_C0, 1),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT, 0x0);

	/* Test that enabling rx after sop prime will set RX_DETECT properly */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C0, 1),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT,
			TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK);

	/* Test error from sop_prime on rx detect set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_RX_DETECT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_sop_prime_enable(USBC_PORT_C0, 0), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test disabling sop prime with rx enabled does change RX_DETECT */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_sop_prime_enable(USBC_PORT_C0, 0),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT,
			TCPC_REG_RX_DETECT_SOP_HRST_MASK);

	/* Test that enabling rx after disabling sop prime set RX_DETECT */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C0, 0),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT, 0x0);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_set_rx_enable(USBC_PORT_C0, 1),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT,
			TCPC_REG_RX_DETECT_SOP_HRST_MASK);
}

/** Test TCPCI get raw message from TCPC */
static void test_tcpci_get_rx_message_raw(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct tcpci_emul_msg msg;
	uint32_t payload[7];
	uint8_t buf[32];
	int exp_head;
	int i, head;
	int size;

	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0x0);
	tcpci_emul_set_reg(emul, TCPC_REG_DEV_CAP_2,
			   TCPC_REG_DEV_CAP_2_LONG_MSG);

	for (i = 0; i < 32; i++) {
		buf[i] = i + 1;
	}
	msg.buf = buf;
	msg.cnt = 32;
	msg.type = TCPCI_MSG_SOP;
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");

	/* Test fail on reading byte count */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_RX_BUFFER);
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C0, payload, &head),
		      NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	/* Get raw message should always clean RX alerts */
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Test too short message */
	msg.cnt = 2;
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C0, payload, &head),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Test too long message */
	msg.cnt = 32;
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C0, payload, &head),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Test alert register and message payload on success */
	size = 28;
	msg.cnt = size + 3;
	msg.type = TCPCI_MSG_SOP_PRIME;
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg, true),
		   "Failed to setup emulator message");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_get_message_raw(USBC_PORT_C0, payload, &head),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);
	/*
	 * Type is in bits 31-28 of header, buf[0] is in bits 7-0,
	 * buf[1] is in bits 15-8
	 */
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf[1] << 8) | buf[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf + 2, size, NULL);
}

/** Test TCPCI get raw message from TCPC revision 2.0 */
static void test_tcpci_get_rx_message_raw_rev2(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	test_tcpci_get_rx_message_raw();
}

/** Test TCPCI get raw message from TCPC revision 1.0 */
static void test_tcpci_get_rx_message_raw_rev1(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	test_tcpci_get_rx_message_raw();
}

/** Test TCPCI transmitting message from TCPC */
static void test_tcpci_transmit(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct tcpci_emul_msg *msg;
	uint32_t data[6];
	uint16_t header;
	int i;

	msg = tcpci_emul_get_tx_msg(emul);

	/* Fill transmit data with pattern */
	for (i = 0; i < 6 * sizeof(uint32_t); i++) {
		((uint8_t *)data)[i] = i;
	}

	/* Test transmit hard reset fail */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TRANSMIT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_transmit(USBC_PORT_C0, TCPCI_MSG_TX_HARD_RESET,
					  0, NULL), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test transmit cabel reset */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C0, TCPCI_MSG_CABLE_RESET,
					  0, NULL), NULL);
	zassert_equal(TCPCI_MSG_CABLE_RESET, msg->type, NULL);

	/* Test transmit hard reset */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C0, TCPCI_MSG_TX_HARD_RESET,
					  0, NULL), NULL);
	zassert_equal(TCPCI_MSG_TX_HARD_RESET, msg->type, NULL);

	/* Test transmit fail on rx buffer */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TX_BUFFER);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_transmit(USBC_PORT_C0, TCPCI_MSG_SOP_PRIME,
					  0, data), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test transmit only header */
	/* Build random header with count 0 */
	header = PD_HEADER(PD_CTRL_PING, PD_ROLE_SOURCE, PD_ROLE_UFP, 0, 0,
			   PD_REV20, 0);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C0, TCPCI_MSG_SOP_PRIME,
					  header, data), NULL);
	zassert_equal(TCPCI_MSG_SOP_PRIME, msg->type, NULL);
	zassert_mem_equal(msg->buf, &header, 2, NULL);
	zassert_equal(2, msg->cnt, NULL);

	/* Test transmit message */
	/* Build random header with count 6 */
	header = PD_HEADER(PD_CTRL_PING, PD_ROLE_SOURCE, PD_ROLE_UFP, 0, 6,
			   PD_REV20, 0);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_transmit(USBC_PORT_C0, TCPCI_MSG_SOP_PRIME,
					  header, data), NULL);
	zassert_equal(TCPCI_MSG_SOP_PRIME, msg->type, NULL);
	zassert_mem_equal(msg->buf, &header, 2, NULL);
	zassert_mem_equal(msg->buf + 2, data, 6 * sizeof(uint32_t), NULL);
	zassert_equal(2 + 6 * sizeof(uint32_t), msg->cnt, NULL);
}

/** Test TCPCI transmitting message from TCPC revision 2.0 */
static void test_tcpci_transmit_rev2(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	test_tcpci_transmit();
}

/** Test TCPCI transmitting message from TCPC revision 1.0 */
static void test_tcpci_transmit_rev1(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	test_tcpci_transmit();
}

/** Test TCPCI alert */
static void test_tcpci_alert(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Test alert read fail */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_ALERT);
	tcpci_tcpc_alert(USBC_PORT_C0);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Handle overcurrent */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_FAULT);
	tcpci_emul_set_reg(emul, TCPC_REG_FAULT_STATUS,
			   TCPC_REG_FAULT_STATUS_VCONN_OVER_CURRENT);
	tcpci_tcpc_alert(USBC_PORT_C0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);
	check_tcpci_reg(emul, TCPC_REG_FAULT_STATUS, 0x0);

	/* Test TX complete */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_TX_COMPLETE);
	tcpci_tcpc_alert(USBC_PORT_C0);

	/* Test clear alert and ext_alert */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_ALERT_EXT);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT_EXT,
			   TCPC_REG_ALERT_EXT_TIMER_EXPIRED);
	tcpci_tcpc_alert(USBC_PORT_C0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);
	check_tcpci_reg(emul, TCPC_REG_FAULT_STATUS, 0x0);

	/* Test CC changed, CC status chosen arbitrary */
	tcpci_emul_set_reg(emul, TCPC_REG_CC_STATUS,
			   TCPC_REG_CC_STATUS_SET(1, TYPEC_CC_VOLT_RP_1_5,
						  TYPEC_CC_VOLT_RP_3_0));
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_CC_STATUS);
	tcpci_tcpc_alert(USBC_PORT_C0);

	/* Test Hard reset */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_HARD_RST);
	tcpci_tcpc_alert(USBC_PORT_C0);
}


/** Test TCPCI alert RX message */
static void test_tcpci_alert_rx_message(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct tcpci_emul_msg msg1, msg2;
	uint8_t buf1[32], buf2[32];
	uint32_t payload[7];
	int exp_head;
	int i, head;
	int size;

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	for (i = 0; i < 32; i++) {
		buf1[i] = i + 1;
		buf2[i] = i + 33;
	}
	size = 23;
	msg1.buf = buf1;
	msg1.cnt = size + 3;
	msg1.type = TCPCI_MSG_SOP;

	msg2.buf = buf2;
	msg2.cnt = size + 3;
	msg2.type = TCPCI_MSG_SOP_PRIME;

	/* Test receiving one message */
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	tcpci_tcpc_alert(USBC_PORT_C0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C0), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C0, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C0), NULL);

	/* Test receiving two messages */
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg2, true),
		   "Failed to setup emulator message");
	tcpci_tcpc_alert(USBC_PORT_C0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C0), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C0, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	/* Check if msg2 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C0), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C0, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf2[1] << 8) | buf2[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf2 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C0), NULL);

	/* Test with too long first message */
	msg1.cnt = 32;
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg2, true),
		   "Failed to setup emulator message");
	tcpci_tcpc_alert(USBC_PORT_C0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg2 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C0), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C0, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf2[1] << 8) | buf2[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf2 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C0), NULL);

	/* Test constant read message failure */
	zassert_ok(tcpci_emul_add_rx_msg(emul, &msg1, true),
		   "Failed to setup emulator message");
	/* Create loop with one message with wrong size */
	msg1.next = &msg1;
	tcpci_tcpc_alert(USBC_PORT_C0);
	/* Nothing should be in queue */
	zassert_false(tcpm_has_pending_message(USBC_PORT_C0), NULL);

	/* Test constant correct messages stream */
	msg1.cnt = size + 3;
	tcpci_tcpc_alert(USBC_PORT_C0);
	msg1.next = NULL;

	/* msg1 should be at least twice in queue */
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	for (i = 0; i < 2; i++) {
		zassert_true(tcpm_has_pending_message(USBC_PORT_C0), NULL);
		zassert_equal(EC_SUCCESS,
			      tcpm_dequeue_message(USBC_PORT_C0, payload,
						   &head), NULL);
		zassert_equal(exp_head, head,
			      "Received header 0x%08lx, expected 0x%08lx",
			      head, exp_head);
		zassert_mem_equal(payload, buf1 + 2, size, NULL);
	}
	tcpm_clear_pending_messages(USBC_PORT_C0);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C0), NULL);

	/* Read message that is left in TCPC buffer */
	tcpci_tcpc_alert(USBC_PORT_C0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(USBC_PORT_C0), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(USBC_PORT_C0, payload,
						       &head), NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(USBC_PORT_C0), NULL);
}

/** Test TCPCI auto discharge on disconnect */
static void test_tcpci_auto_discharge(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	uint8_t initial_ctrl;
	uint8_t exp_ctrl;

	/* Set initial value for POWER ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
		       TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_CTRL, initial_ctrl);

	/* Test discharge enable */
	exp_ctrl = initial_ctrl | TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
	tcpci_tcpc_enable_auto_discharge_disconnect(USBC_PORT_C0, 1);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test discharge disable */
	exp_ctrl = initial_ctrl &
		   ~TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
	tcpci_tcpc_enable_auto_discharge_disconnect(USBC_PORT_C0, 0);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);
}

/** Test TCPCI drp toggle */
static void test_tcpci_drp_toggle(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint8_t exp_tcpc_ctrl, exp_role_ctrl, initial_tcpc_ctrl;

	/* Set TCPCI to revision 2 */
	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Test error on failed role CTRL set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpc_drp_toggle(USBC_PORT_C0),
		      NULL);

	/* Test error on failed TCPC CTRL set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpc_drp_toggle(USBC_PORT_C0),
		      NULL);

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpc_drp_toggle(USBC_PORT_C0),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set initial value for TCPC ctrl register. Chosen arbitrary. */
	initial_tcpc_ctrl = TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL |
			    TCPC_REG_TCPC_CTRL_BIST_TEST_MODE;
	tcpci_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, initial_tcpc_ctrl);

	/*
	 * Test correct registers values for rev 2.0. Role control CC lines
	 * have to be set to RP with DRP enabled and smallest RP value.
	 */
	exp_tcpc_ctrl = initial_tcpc_ctrl |
			TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT;
	exp_role_ctrl = TCPC_REG_ROLE_CTRL_SET(TYPEC_DRP, TYPEC_RP_USB,
					       TYPEC_CC_RP, TYPEC_CC_RP);
	zassert_equal(EC_SUCCESS, tcpci_tcpc_drp_toggle(USBC_PORT_C0), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_tcpc_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL, exp_role_ctrl);
	check_tcpci_reg(emul, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);

	/* Set TCPCI to revision 1 */
	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	/* Set initial value for TCPC ctrl register. Chosen arbitrary. */
	initial_tcpc_ctrl = TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL |
			    TCPC_REG_TCPC_CTRL_BIST_TEST_MODE;
	tcpci_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, initial_tcpc_ctrl);

	/*
	 * Test correct registers values for rev 1.0. Role control CC lines
	 * have to be set to RD with DRP enabled and smallest RP value.
	 * Only CC lines setting is different from rev 2.0
	 */
	exp_tcpc_ctrl = initial_tcpc_ctrl |
			TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT;
	exp_role_ctrl = TCPC_REG_ROLE_CTRL_SET(TYPEC_DRP, TYPEC_RP_USB,
					       TYPEC_CC_RD, TYPEC_CC_RD);
	zassert_equal(EC_SUCCESS, tcpci_tcpc_drp_toggle(USBC_PORT_C0), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_tcpc_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL, exp_role_ctrl);
	check_tcpci_reg(emul, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);
}

/** Test TCPCI get chip info */
static void test_tcpci_get_chip_info(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct ec_response_pd_chip_info_v1 info;
	uint16_t vendor, product, bcd;

	/* Test error on failed vendor id get */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_VENDOR_ID);
	zassert_equal(EC_ERROR_INVAL, tcpci_get_chip_info(USBC_PORT_C0, 1,
							  &info), NULL);

	/* Test error on failed product id get */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_PRODUCT_ID);
	zassert_equal(EC_ERROR_INVAL, tcpci_get_chip_info(USBC_PORT_C0, 1,
							  &info), NULL);

	/* Test error on failed BCD get */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_VENDOR_ID);
	zassert_equal(EC_ERROR_INVAL, tcpci_get_chip_info(USBC_PORT_C0, 1,
							  &info), NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test reading chip info. Values chosen arbitrary. */
	vendor = 0x1234;
	product = 0x5678;
	bcd = 0x9876;
	tcpci_emul_set_reg(emul, TCPC_REG_VENDOR_ID, vendor);
	tcpci_emul_set_reg(emul, TCPC_REG_PRODUCT_ID, product);
	tcpci_emul_set_reg(emul, TCPC_REG_BCD_DEV, bcd);
	zassert_equal(EC_SUCCESS, tcpci_get_chip_info(USBC_PORT_C0, 1, &info),
		      NULL);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(bcd, info.device_id, NULL);

	/* Test reading cached chip info */
	info.vendor_id = 0;
	info.product_id = 0;
	info.device_id = 0;
	/* Make sure, that TCPC is not accessed */
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS, tcpci_get_chip_info(USBC_PORT_C0, 0, &info),
		      NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(bcd, info.device_id, NULL);
}

/** Test TCPCI enter low power mode */
static void test_tcpci_low_power_mode(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, tcpci_enter_low_power_mode(USBC_PORT_C0),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct command is issued */
	zassert_equal(EC_SUCCESS, tcpci_enter_low_power_mode(USBC_PORT_C0),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

/** Test TCPCI set bist test mode */
static void test_tcpci_set_bist_mode(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint16_t exp_mask, initial_mask;
	uint8_t exp_ctrl, initial_ctrl;

	/* Test error on TCPC CTRL set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL, tcpci_set_bist_test_mode(USBC_PORT_C0, 1),
		      NULL);

	/* Test error on alert mask set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ALERT_MASK);
	zassert_equal(EC_ERROR_INVAL, tcpci_set_bist_test_mode(USBC_PORT_C0, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set initial value for alert mask register. Chosen arbitrary. */
	initial_mask = TCPC_REG_ALERT_MASK_ALL;
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT_MASK, initial_mask);

	/* Set initial value for TCPC ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL |
		       TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT;
	tcpci_emul_set_reg(emul, TCPC_REG_TCPC_CTRL, initial_ctrl);

	/* Test enabling bist test mode */
	exp_mask = initial_mask & ~TCPC_REG_ALERT_RX_STATUS;
	exp_ctrl = initial_ctrl | TCPC_REG_TCPC_CTRL_BIST_TEST_MODE;
	zassert_equal(EC_SUCCESS, tcpci_set_bist_test_mode(USBC_PORT_C0, 1),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Test disabling bist test mode */
	exp_mask = initial_mask | TCPC_REG_ALERT_RX_STATUS;
	exp_ctrl = initial_ctrl & ~TCPC_REG_TCPC_CTRL_BIST_TEST_MODE;
	zassert_equal(EC_SUCCESS, tcpci_set_bist_test_mode(USBC_PORT_C0, 0),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);
}

/** Test TCPCI discharge vbus */
static void test_tcpci_discharge_vbus(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	uint8_t exp_ctrl, initial_ctrl;

	/* Set initial value for POWER ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
		       TCPC_REG_POWER_CTRL_VOLT_ALARM_DIS;
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_CTRL, initial_ctrl);

	/* Test discharge enable */
	exp_ctrl = initial_ctrl | TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_tcpc_discharge_vbus(USBC_PORT_C0, 1);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test discharge disable */
	exp_ctrl = initial_ctrl & ~TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_tcpc_discharge_vbus(USBC_PORT_C0, 0);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);
}

/** Test TCPC xfer */
static void test_tcpc_xfer(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	uint16_t val, exp_val;
	uint8_t reg;

	/* Set value to register (value and register chosen arbitrary) */
	exp_val = 0x7fff;
	reg = TCPC_REG_ALERT_MASK;
	tcpci_emul_set_reg(emul, reg, exp_val);

	/* Test reading value using tcpc_xfer() function */
	zassert_equal(EC_SUCCESS,
		      tcpc_xfer(USBC_PORT_C0, &reg, 1, (uint8_t *)&val, 2),
		      NULL);
	zassert_equal(exp_val, val, "0x%x != 0x%x", exp_val, val);
}

/** Test TCPCI debug accessory enable/disable */
static void test_tcpci_debug_accessory(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	uint8_t exp_val, initial_val;

	/* Set initial value for STD output register. Chosen arbitrary. */
	initial_val = TCPC_REG_CONFIG_STD_OUTPUT_AUDIO_CONN_N |
		      TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB |
		      TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED |
		      TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_emul_set_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, initial_val);

	/* Test debug accessory connect */
	exp_val = initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_tcpc_debug_accessory(USBC_PORT_C0, 1);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);

	/* Test debug accessory disconnect */
	exp_val = initial_val | TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_tcpc_debug_accessory(USBC_PORT_C0, 0);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
}

/* Setup TCPCI usb mux to behave as it is used only for usb mux */
static void set_usb_mux_not_tcpc(void)
{
	usb_muxes[USBC_PORT_C0].flags = USB_MUX_FLAG_NOT_TCPC;
}

/* Setup TCPCI usb mux to behave as it is used for usb mux and TCPC */
static void set_usb_mux_tcpc(void)
{
	usb_muxes[USBC_PORT_C0].flags = 0;
}

/** Test TCPCI mux init */
static void test_tcpci_mux_init(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct usb_mux *tcpci_usb_mux = &usb_muxes[USBC_PORT_C0];

	/* Set as usb mux with TCPC for first init call */
	set_usb_mux_tcpc();

	/* Make sure that TCPC is not accessed */
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);

	/* Set as only usb mux without TCPC for rest of the test */
	set_usb_mux_not_tcpc();

	/* Test fail on power status read */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_POWER_STATUS);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on uninitialised bit set */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT,
		      tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);

	/* Set correct power status for rest of the test */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS, 0);

	/* Test fail on alert mask write fail */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ALERT_MASK);
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);

	/* Test fail on alert write fail */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ALERT);
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set arbitrary value to alert and alert mask registers */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0xffff);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT_MASK, 0xffff);

	/* Test success init */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0);
}

/** Test TCPCI mux enter low power mode */
static void test_tcpci_mux_enter_low_power(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct usb_mux *tcpci_usb_mux = &usb_muxes[USBC_PORT_C0];

	/* Set as usb mux with TCPC for first enter_low_power call */
	set_usb_mux_tcpc();

	/* Make sure that TCPC is not accessed */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);

	/* Set as only usb mux without TCPC for rest of the test */
	set_usb_mux_not_tcpc();

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct command is issued */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

/** Test TCPCI mux set and get */
static void test_tcpci_mux_set_get(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct usb_mux *tcpci_usb_mux = &usb_muxes[USBC_PORT_C0];
	mux_state_t mux_state, mux_state_get;
	uint16_t exp_val, initial_val;
	bool ack;

	mux_state = USB_PD_MUX_NONE;

	/* Test fail on standard output config register read */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_CONFIG_STD_OUTPUT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on standard output config register write */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   TCPC_REG_CONFIG_STD_OUTPUT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set initial value for STD output register. Chosen arbitrary. */
	initial_val = TCPC_REG_CONFIG_STD_OUTPUT_AUDIO_CONN_N |
		      TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB |
		      TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED |
		      TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_emul_set_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, initial_val);

	/* Test setting/getting no MUX connection without polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_NONE;
	exp_val &= ~TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_NONE;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX DP with polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP |
		  TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX USB without polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	exp_val &= ~TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_USB_ENABLED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX USB and DP with polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB |
		  TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
		    USB_PD_MUX_POLARITY_INVERTED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);
}

void test_suite_tcpci(void)
{
	/* This test suite assumes that first usb mux for port C0 is TCPCI */
	__ASSERT(usb_muxes[USBC_PORT_C0].driver == &tcpci_tcpm_usb_mux_driver,
		 "Invalid config of usb_muxes in test/drivers/src/stubs.c");

	ztest_test_suite(tcpci,
			 ztest_user_unit_test(test_tcpci_init),
			 ztest_user_unit_test(test_tcpci_release),
			 ztest_user_unit_test(test_tcpci_get_cc),
			 ztest_user_unit_test(test_tcpci_set_cc),
			 ztest_user_unit_test(test_tcpci_set_polarity),
			 ztest_user_unit_test(test_tcpci_set_vconn),
			 ztest_user_unit_test(test_tcpci_set_msg_header),
			 ztest_user_unit_test(test_tcpci_set_rx_detect),
			 ztest_user_unit_test(
				test_tcpci_get_rx_message_raw_rev2),
			 ztest_user_unit_test(test_tcpci_transmit_rev2),
			 ztest_user_unit_test(
				test_tcpci_get_rx_message_raw_rev1),
			 ztest_user_unit_test(test_tcpci_transmit_rev1),
			 ztest_user_unit_test(test_tcpci_alert),
			 ztest_user_unit_test(test_tcpci_alert_rx_message),
			 ztest_user_unit_test(test_tcpci_auto_discharge),
			 ztest_user_unit_test(test_tcpci_drp_toggle),
			 ztest_user_unit_test(test_tcpci_get_chip_info),
			 ztest_user_unit_test(test_tcpci_low_power_mode),
			 ztest_user_unit_test(test_tcpci_set_bist_mode),
			 ztest_user_unit_test(test_tcpci_discharge_vbus),
			 ztest_user_unit_test(test_tcpc_xfer),
			 ztest_user_unit_test(test_tcpci_debug_accessory),
			 ztest_user_unit_test(test_tcpci_mux_init),
			 ztest_user_unit_test(test_tcpci_mux_enter_low_power),
			 /* Test set/get with usb mux and without TCPC */
			 ztest_unit_test_setup_teardown(test_tcpci_mux_set_get,
				set_usb_mux_not_tcpc, set_usb_mux_tcpc),
			 /* Test set/get with usb mux and TCPC */
			 ztest_user_unit_test(test_tcpci_mux_set_get));
	ztest_run_test_suite(tcpci);
}
