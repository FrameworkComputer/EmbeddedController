/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpci_test_common.h"

#include "tcpm/tcpci.h"

/** Check TCPC register value */
void check_tcpci_reg_f(const struct emul *emul, int reg, uint16_t exp_val,
		       int line)
{
	uint16_t reg_val;

	zassert_ok(tcpci_emul_get_reg(emul, reg, &reg_val),
		   "Failed tcpci_emul_get_reg(); line: %d", line);
	zassert_equal(exp_val, reg_val, "Expected 0x%x, got 0x%x; line: %d",
		      exp_val, reg_val, line);
}

/** Check TCPC register value with mask */
void check_tcpci_reg_with_mask_f(const struct emul *emul, int reg,
				 uint16_t exp_val, uint16_t mask, int line)
{
	uint16_t reg_val;

	zassert_ok(tcpci_emul_get_reg(emul, reg, &reg_val),
		   "Failed tcpci_emul_get_reg(); line: %d", line);
	zassert_equal(exp_val & mask, reg_val & mask,
		      "Expected 0x%x, got 0x%x, mask 0x%x; line: %d",
		      exp_val, reg_val, mask, line);
}

/** Test TCPCI init and vbus level */
void test_tcpci_init(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint16_t exp_mask;

	tcpc_config[port].flags = TCPC_FLAGS_TCPCI_REV2_0 &
				  TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Test fail on power status read */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_POWER_STATUS);
	zassert_equal(EC_ERROR_INVAL, drv->init(port), NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on uninitialised bit set */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT, drv->init(port), NULL);

	/*
	 * Set expected alert mask. It is used in test until vSafe0V tcpc
	 * config flag is revmoved.
	 */
	exp_mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		   TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		   TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS |
		   TCPC_REG_ALERT_FAULT | TCPC_REG_ALERT_POWER_STATUS;

	/* Set TCPCI emulator VBUS to safe0v (disconnected) */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);

	/* Test init with VBUS safe0v without vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, drv->init(port), NULL);
	zassert_true(drv->check_vbus_level(port, VBUS_SAFE0V), NULL);
	zassert_false(drv->check_vbus_level(port, VBUS_PRESENT), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Set TCPCI emulator VBUS to present (connected, above 4V) */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_PRES |
			   TCPC_REG_POWER_STATUS_VBUS_DET);

	/* Test init with VBUS present without vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, drv->init(port), NULL);
	zassert_false(drv->check_vbus_level(port, VBUS_SAFE0V), NULL);
	zassert_true(drv->check_vbus_level(port, VBUS_PRESENT), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Disable vSafe0V tcpc config flag and update expected alert mask */
	exp_mask |= TCPC_REG_ALERT_EXT_STATUS;
	tcpc_config[port].flags = TCPC_FLAGS_TCPCI_REV2_0;

	/* Test init with VBUS present with vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, drv->init(port), NULL);
	zassert_false(drv->check_vbus_level(port, VBUS_SAFE0V), NULL);
	zassert_true(drv->check_vbus_level(port, VBUS_PRESENT), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Set TCPCI emulator VBUS to safe0v (disconnected) */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	/* Test init with VBUS safe0v with vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, drv->init(port), NULL);
	zassert_true(drv->check_vbus_level(port, VBUS_SAFE0V), NULL);
	zassert_false(drv->check_vbus_level(port, VBUS_PRESENT), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/*
	 * Set TCPCI emulator VBUS to disconnected but not at vSafe0V
	 * (VBUS in 0.8V - 3.5V range)
	 */
	tcpci_emul_set_reg(emul, TCPC_REG_EXT_STATUS, 0);

	/* Test init with VBUS not safe0v with vSafe0V tcpc config flag */
	zassert_equal(EC_SUCCESS, drv->init(port), NULL);
	zassert_false(drv->check_vbus_level(port, VBUS_SAFE0V), NULL);
	zassert_false(drv->check_vbus_level(port, VBUS_PRESENT), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK,
			TCPC_REG_POWER_STATUS_VBUS_PRES);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);
}

/** Test TCPCI release */
void test_tcpci_release(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;

	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0xffff);

	zassert_equal(EC_SUCCESS, drv->release(port), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_STATUS_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0);
}

/** Test TCPCI get cc */
void test_tcpci_get_cc(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
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
		zassert_equal(EC_SUCCESS, drv->get_cc(port, &cc1, &cc2),
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
void test_tcpci_set_cc(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	enum tcpc_rp_value rp;
	enum tcpc_cc_pull cc;

	/* Test setting default RP and cc open */
	rp = TYPEC_RP_USB;
	cc = TYPEC_CC_OPEN;
	zassert_equal(EC_SUCCESS, drv->select_rp_value(port, rp), NULL);
	zassert_equal(EC_SUCCESS, drv->set_cc(port, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));

	/* Test error on failed role ctrl set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL, drv->set_cc(port, TYPEC_CC_OPEN), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting 1.5 RP and cc RD */
	rp = TYPEC_RP_1A5;
	cc = TYPEC_CC_RD;
	zassert_equal(EC_SUCCESS, drv->select_rp_value(port, rp), NULL);
	zassert_equal(EC_SUCCESS, drv->set_cc(port, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));

	/* Test setting 3.0 RP and cc RP */
	rp = TYPEC_RP_3A0;
	cc = TYPEC_CC_RP;
	zassert_equal(EC_SUCCESS, drv->select_rp_value(port, rp), NULL);
	zassert_equal(EC_SUCCESS, drv->set_cc(port, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));

	/*
	 * Test setting 3.0 RP and cc RA. drv->select_rp_value() is
	 * intentionally not called to check if selected rp is persistent.
	 */
	cc = TYPEC_CC_RA;
	zassert_equal(EC_SUCCESS, drv->set_cc(port, cc), NULL);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc));
}

/** Test TCPCI set polarity */
void test_tcpci_set_polarity(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
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
	zassert_equal(EC_ERROR_INVAL, drv->set_polarity(port, POLARITY_CC2),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC2 */
	exp_ctrl = initial_ctrl | TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, drv->set_polarity(port, POLARITY_CC2), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC1 */
	exp_ctrl = initial_ctrl & ~TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, drv->set_polarity(port, POLARITY_CC1), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC2 DTS */
	exp_ctrl = initial_ctrl | TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, drv->set_polarity(port, POLARITY_CC2_DTS),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);

	/* Test setting polarity CC1 DTS */
	exp_ctrl = initial_ctrl & ~TCPC_REG_TCPC_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, drv->set_polarity(port, POLARITY_CC1_DTS),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
}

/** Test TCPCI set vconn */
void test_tcpci_set_vconn(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
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
	zassert_equal(EC_ERROR_INVAL, drv->set_vconn(port, 1), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test vconn enable */
	exp_ctrl = initial_ctrl | TCPC_REG_POWER_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, drv->set_vconn(port, 1), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test vconn disable */
	exp_ctrl = initial_ctrl & ~TCPC_REG_POWER_CTRL_SET(1);
	zassert_equal(EC_SUCCESS, drv->set_vconn(port, 0), NULL);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);
}

/** Test TCPCI set msg header */
void test_tcpci_set_msg_header(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	/* Test error on failed header set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_MSG_HDR_INFO);
	zassert_equal(EC_ERROR_INVAL, drv->set_msg_header(port, PD_ROLE_SINK,
							  PD_ROLE_UFP), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test setting sink UFP */
	zassert_equal(EC_SUCCESS, drv->set_msg_header(port, PD_ROLE_SINK,
						      PD_ROLE_UFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_UFP, PD_ROLE_SINK));

	/* Test setting sink DFP */
	zassert_equal(EC_SUCCESS, drv->set_msg_header(port, PD_ROLE_SINK,
						      PD_ROLE_DFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_DFP, PD_ROLE_SINK));

	/* Test setting source UFP */
	zassert_equal(EC_SUCCESS, drv->set_msg_header(port, PD_ROLE_SOURCE,
						      PD_ROLE_UFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_UFP, PD_ROLE_SOURCE));

	/* Test setting source DFP */
	zassert_equal(EC_SUCCESS, drv->set_msg_header(port, PD_ROLE_SOURCE,
						      PD_ROLE_DFP), NULL);
	check_tcpci_reg(emul, TCPC_REG_MSG_HDR_INFO,
			TCPC_REG_MSG_HDR_INFO_SET(PD_ROLE_DFP, PD_ROLE_SOURCE));
}

/** Test TCPCI rx and sop prime enable */
void test_tcpci_set_rx_detect(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	/* Test error from rx_enable on rx detect set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_RX_DETECT);
	zassert_equal(EC_ERROR_INVAL, drv->set_rx_enable(port, 1), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test rx disable */
	zassert_equal(EC_SUCCESS, drv->set_rx_enable(port, 0), NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT, 0x0);

	/* Test setting sop prime with rx disable doesn't change RX_DETECT */
	zassert_equal(EC_SUCCESS, drv->sop_prime_enable(port, 1), NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT, 0x0);

	/* Test that enabling rx after sop prime will set RX_DETECT properly */
	zassert_equal(EC_SUCCESS, drv->set_rx_enable(port, 1), NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT,
			TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK);

	/* Test error from sop_prime on rx detect set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_RX_DETECT);
	zassert_equal(EC_ERROR_INVAL, drv->sop_prime_enable(port, 0), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test disabling sop prime with rx enabled does change RX_DETECT */
	zassert_equal(EC_SUCCESS, drv->sop_prime_enable(port, 0), NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT,
			TCPC_REG_RX_DETECT_SOP_HRST_MASK);

	/* Test that enabling rx after disabling sop prime set RX_DETECT */
	zassert_equal(EC_SUCCESS, drv->set_rx_enable(port, 0), NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT, 0x0);
	zassert_equal(EC_SUCCESS, drv->set_rx_enable(port, 1), NULL);
	check_tcpci_reg(emul, TCPC_REG_RX_DETECT,
			TCPC_REG_RX_DETECT_SOP_HRST_MASK);
}

/** Test TCPCI get raw message from TCPC */
void test_tcpci_get_rx_message_raw(const struct emul *emul,
				   enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct tcpci_emul_msg msg;
	uint32_t payload[7];
	uint16_t rx_mask;
	uint8_t buf[32];
	int exp_head;
	int i, head;
	int size;

	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0x0);
	tcpci_emul_set_reg(emul, TCPC_REG_DEV_CAP_2,
			   TCPC_REG_DEV_CAP_2_LONG_MSG);
	tcpci_emul_set_reg(emul, TCPC_REG_RX_DETECT,
			   TCPC_REG_RX_DETECT_SOP | TCPC_REG_RX_DETECT_SOPP);

	for (i = 0; i < 32; i++) {
		buf[i] = i + 1;
	}
	msg.buf = buf;
	msg.cnt = 31;
	msg.type = TCPCI_MSG_SOP;
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");

	/* Test fail on reading byte count */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_RX_BUFFER);
	zassert_equal(EC_ERROR_UNKNOWN,
		      drv->get_message_raw(port, payload, &head), NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	/* Get raw message should always clean RX alerts */
	rx_mask = TCPC_REG_ALERT_RX_BUF_OVF | TCPC_REG_ALERT_RX_STATUS;
	check_tcpci_reg_with_mask(emul, TCPC_REG_ALERT, 0x0, rx_mask);

	/* Test too short message */
	msg.cnt = 1;
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");
	zassert_equal(EC_ERROR_UNKNOWN,
		      drv->get_message_raw(port, payload, &head), NULL);
	check_tcpci_reg_with_mask(emul, TCPC_REG_ALERT, 0x0, rx_mask);

	/* Test too long message */
	msg.cnt = 31;
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");
	zassert_equal(EC_ERROR_UNKNOWN,
		      drv->get_message_raw(port, payload, &head), NULL);
	check_tcpci_reg_with_mask(emul, TCPC_REG_ALERT, 0x0, rx_mask);

	/* Test alert register and message payload on success */
	size = 28;
	msg.cnt = size + 2;
	msg.type = TCPCI_MSG_SOP_PRIME;
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");
	zassert_equal(EC_SUCCESS, drv->get_message_raw(port, payload, &head),
		      NULL);
	check_tcpci_reg_with_mask(emul, TCPC_REG_ALERT, 0x0, rx_mask);
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

/** Test TCPCI transmitting message from TCPC */
void test_tcpci_transmit(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
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
		      drv->transmit(port, TCPCI_MSG_TX_HARD_RESET, 0, NULL),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test transmit cabel reset */
	zassert_equal(EC_SUCCESS,
		      drv->transmit(port, TCPCI_MSG_CABLE_RESET, 0, NULL),
		      NULL);
	zassert_equal(TCPCI_MSG_CABLE_RESET, msg->type, NULL);

	/* Test transmit hard reset */
	zassert_equal(EC_SUCCESS,
		      drv->transmit(port, TCPCI_MSG_TX_HARD_RESET, 0, NULL),
		      NULL);
	zassert_equal(TCPCI_MSG_TX_HARD_RESET, msg->type, NULL);

	/* Test transmit fail on rx buffer */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TX_BUFFER);
	zassert_equal(EC_ERROR_INVAL,
		      drv->transmit(port, TCPCI_MSG_SOP_PRIME, 0, data),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test transmit only header */
	/* Build random header with count 0 */
	header = PD_HEADER(PD_CTRL_PING, PD_ROLE_SOURCE, PD_ROLE_UFP, 0, 0,
			   PD_REV20, 0);
	zassert_equal(EC_SUCCESS,
		      drv->transmit(port, TCPCI_MSG_SOP_PRIME, header, data),
		      NULL);
	zassert_equal(TCPCI_MSG_SOP_PRIME, msg->type, NULL);
	zassert_mem_equal(msg->buf, &header, 2, NULL);
	zassert_equal(2, msg->cnt, NULL);

	/* Test transmit message */
	/* Build random header with count 6 */
	header = PD_HEADER(PD_CTRL_PING, PD_ROLE_SOURCE, PD_ROLE_UFP, 0, 6,
			   PD_REV20, 0);
	zassert_equal(EC_SUCCESS,
		      drv->transmit(port, TCPCI_MSG_SOP_PRIME, header, data),
		      NULL);
	zassert_equal(TCPCI_MSG_SOP_PRIME, msg->type, NULL);
	zassert_mem_equal(msg->buf, &header, 2, NULL);
	zassert_mem_equal(msg->buf + 2, data, 6 * sizeof(uint32_t), NULL);
	zassert_equal(2 + 6 * sizeof(uint32_t), msg->cnt, NULL);
}

/** Test TCPCI alert */
void test_tcpci_alert(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	tcpc_config[port].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Test alert read fail */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_ALERT);
	drv->tcpc_alert(port);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Handle overcurrent */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_FAULT);
	tcpci_emul_set_reg(emul, TCPC_REG_FAULT_STATUS,
			   TCPC_REG_FAULT_STATUS_VCONN_OVER_CURRENT);
	drv->tcpc_alert(port);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);
	check_tcpci_reg(emul, TCPC_REG_FAULT_STATUS, 0x0);

	/* Test TX complete */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_TX_COMPLETE);
	drv->tcpc_alert(port);

	/* Test clear alert and ext_alert */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_ALERT_EXT);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT_EXT,
			   TCPC_REG_ALERT_EXT_TIMER_EXPIRED);
	drv->tcpc_alert(port);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);
	check_tcpci_reg(emul, TCPC_REG_FAULT_STATUS, 0x0);

	/* Test CC changed, CC status chosen arbitrary */
	tcpci_emul_set_reg(emul, TCPC_REG_CC_STATUS,
			   TCPC_REG_CC_STATUS_SET(1, TYPEC_CC_VOLT_RP_1_5,
						  TYPEC_CC_VOLT_RP_3_0));
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_CC_STATUS);
	drv->tcpc_alert(port);

	/* Test Hard reset */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_HARD_RST);
	drv->tcpc_alert(port);
}

/** Test TCPCI alert RX message */
void test_tcpci_alert_rx_message(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct tcpci_emul_msg msg1, msg2;
	uint8_t buf1[32], buf2[32];
	uint32_t payload[7];
	int exp_head;
	int i, head;
	int size;

	tcpc_config[port].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);
	tcpci_emul_set_reg(emul, TCPC_REG_DEV_CAP_2,
			   TCPC_REG_DEV_CAP_2_LONG_MSG);
	tcpci_emul_set_reg(emul, TCPC_REG_RX_DETECT,
			   TCPC_REG_RX_DETECT_SOP | TCPC_REG_RX_DETECT_SOPP);

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
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg1, true),
		      "Failed to setup emulator message");
	drv->tcpc_alert(port);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(port), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(port, payload, &head),
		      NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(port), NULL);

	/* Test receiving two messages */
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg1, true),
		      "Failed to setup emulator message");
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg2, true),
		      "Failed to setup emulator message");
	drv->tcpc_alert(port);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(port), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(port, payload, &head),
		      NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	/* Check if msg2 is in queue */
	zassert_true(tcpm_has_pending_message(port), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(port, payload, &head),
		      NULL);
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf2[1] << 8) | buf2[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf2 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(port), NULL);

	/* Test with too long first message */
	msg1.cnt = 32;
	tcpci_emul_set_reg(emul, TCPC_REG_DEV_CAP_2,
			   TCPC_REG_DEV_CAP_2_LONG_MSG);
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg1, true),
		      "Failed to setup emulator message");
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg2, true),
		      "Failed to setup emulator message");
	drv->tcpc_alert(port);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg2 is in queue */
	zassert_true(tcpm_has_pending_message(port), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(port, payload, &head),
		      NULL);
	exp_head = (TCPCI_MSG_SOP_PRIME << 28) | (buf2[1] << 8) | buf2[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf2 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(port), NULL);

	/* Test constant read message failure */
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg1, true),
		      "Failed to setup emulator message");
	/* Create loop with one message with wrong size */
	msg1.next = &msg1;
	drv->tcpc_alert(port);
	/* Nothing should be in queue */
	zassert_false(tcpm_has_pending_message(port), NULL);

	/* Test constant correct messages stream */
	msg1.cnt = size + 3;
	drv->tcpc_alert(port);
	msg1.next = NULL;

	/* msg1 should be at least twice in queue */
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	for (i = 0; i < 2; i++) {
		zassert_true(tcpm_has_pending_message(port), NULL);
		zassert_equal(EC_SUCCESS,
			      tcpm_dequeue_message(port, payload, &head), NULL);
		zassert_equal(exp_head, head,
			      "Received header 0x%08lx, expected 0x%08lx",
			      head, exp_head);
		zassert_mem_equal(payload, buf1 + 2, size, NULL);
	}
	tcpm_clear_pending_messages(port);
	zassert_false(tcpm_has_pending_message(port), NULL);

	/* Read message that is left in TCPC buffer */
	drv->tcpc_alert(port);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0x0);

	/* Check if msg1 is in queue */
	zassert_true(tcpm_has_pending_message(port), NULL);
	zassert_equal(EC_SUCCESS, tcpm_dequeue_message(port, payload, &head),
		      NULL);
	exp_head = (TCPCI_MSG_SOP << 28) | (buf1[1] << 8) | buf1[0];
	zassert_equal(exp_head, head,
		      "Received header 0x%08lx, expected 0x%08lx",
		      head, exp_head);
	zassert_mem_equal(payload, buf1 + 2, size, NULL);
	zassert_false(tcpm_has_pending_message(port), NULL);
}

/** Test TCPCI auto discharge on disconnect */
void test_tcpci_auto_discharge(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	uint8_t initial_ctrl;
	uint8_t exp_ctrl;

	/* Set initial value for POWER ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
		       TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_CTRL, initial_ctrl);

	/* Test discharge enable */
	exp_ctrl = initial_ctrl | TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
	drv->tcpc_enable_auto_discharge_disconnect(port, 1);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test discharge disable */
	exp_ctrl = initial_ctrl &
		   ~TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
	drv->tcpc_enable_auto_discharge_disconnect(port, 0);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);
}

/** Test TCPCI drp toggle */
void test_tcpci_drp_toggle(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint8_t exp_tcpc_ctrl, exp_role_ctrl, initial_tcpc_ctrl;

	/* Set TCPCI to revision 2 */
	tcpc_config[port].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Test error on failed role CTRL set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL, drv->drp_toggle(port), NULL);

	/* Test error on failed TCPC CTRL set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL, drv->drp_toggle(port), NULL);

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, drv->drp_toggle(port), NULL);
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
	zassert_equal(EC_SUCCESS, drv->drp_toggle(port), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_tcpc_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL, exp_role_ctrl);
	check_tcpci_reg(emul, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);

	/* Set TCPCI to revision 1 */
	tcpc_config[port].flags = 0;
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
	zassert_equal(EC_SUCCESS, drv->drp_toggle(port), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_tcpc_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ROLE_CTRL, exp_role_ctrl);
	check_tcpci_reg(emul, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);
}

/** Test TCPCI get chip info */
void test_tcpci_get_chip_info(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct ec_response_pd_chip_info_v1 info;
	uint16_t vendor, product, bcd;

	/* Test error on failed vendor id get */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_VENDOR_ID);
	zassert_equal(EC_ERROR_INVAL, drv->get_chip_info(port, 1, &info), NULL);

	/* Test error on failed product id get */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_PRODUCT_ID);
	zassert_equal(EC_ERROR_INVAL, drv->get_chip_info(port, 1, &info), NULL);

	/* Test error on failed BCD get */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_VENDOR_ID);
	zassert_equal(EC_ERROR_INVAL, drv->get_chip_info(port, 1, &info), NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test reading chip info. Values chosen arbitrary. */
	vendor = 0x1234;
	product = 0x5678;
	bcd = 0x9876;
	tcpci_emul_set_reg(emul, TCPC_REG_VENDOR_ID, vendor);
	tcpci_emul_set_reg(emul, TCPC_REG_PRODUCT_ID, product);
	tcpci_emul_set_reg(emul, TCPC_REG_BCD_DEV, bcd);
	zassert_equal(EC_SUCCESS, drv->get_chip_info(port, 1, &info), NULL);
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
	zassert_equal(EC_SUCCESS, drv->get_chip_info(port, 0, &info), NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(bcd, info.device_id, NULL);
}

/** Test TCPCI enter low power mode */
void test_tcpci_low_power_mode(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, drv->enter_low_power_mode(port), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct command is issued */
	zassert_equal(EC_SUCCESS, drv->enter_low_power_mode(port), NULL);
	check_tcpci_reg(emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

/** Test TCPCI set bist test mode */
void test_tcpci_set_bist_mode(const struct emul *emul, enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	uint16_t exp_mask, initial_mask;
	uint8_t exp_ctrl, initial_ctrl;

	/* Test error on TCPC CTRL set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_TCPC_CTRL);
	zassert_equal(EC_ERROR_INVAL, drv->set_bist_test_mode(port, 1), NULL);

	/* Test error on alert mask set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ALERT_MASK);
	zassert_equal(EC_ERROR_INVAL, drv->set_bist_test_mode(port, 1), NULL);
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
	zassert_equal(EC_SUCCESS, drv->set_bist_test_mode(port, 1), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);

	/* Test disabling bist test mode */
	exp_mask = initial_mask | TCPC_REG_ALERT_RX_STATUS;
	exp_ctrl = initial_ctrl & ~TCPC_REG_TCPC_CTRL_BIST_TEST_MODE;
	zassert_equal(EC_SUCCESS, drv->set_bist_test_mode(port, 0), NULL);
	check_tcpci_reg(emul, TCPC_REG_TCPC_CTRL, exp_ctrl);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, exp_mask);
}
