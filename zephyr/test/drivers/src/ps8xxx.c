/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_tcpci.h"
#include "emul/emul_ps8xxx.h"
#include "timer.h"
#include "i2c.h"
#include "stubs.h"
#include "tcpci_test_common.h"

#include "tcpm/tcpci.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/ps8xxx_public.h"

#define PS8XXX_EMUL_LABEL	DT_LABEL(DT_NODELABEL(ps8xxx_emul))

/** Test PS8xxx init fail conditions common for all PS8xxx devices */
static void test_ps8xxx_init_fail(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);

	/* Test fail on FW reg read */
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul, PS8XXX_REG_FW_REV);
	zassert_equal(EC_ERROR_TIMEOUT, ps8xxx_tcpm_drv.init(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on FW reg set to 0 */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0x0);
	zassert_equal(EC_ERROR_TIMEOUT, ps8xxx_tcpm_drv.init(USBC_PORT_C1),
		      NULL);

	/* Set arbitrary FW reg value != 0 for rest of the test */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0x31);

	/* Test fail on TCPCI init */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT, ps8xxx_tcpm_drv.init(USBC_PORT_C1),
		      NULL);
}

/**
 * Test PS8805 init and indirectly ps8705_dci_disable which is
 * used by PS8805
 */
static void test_ps8805_init(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *p1_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_1);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);

	/* Set arbitrary FW reg value != 0 for this test */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0x31);
	/* Set correct power status for this test */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS, 0x0);

	/* Test fail on read I2C debug reg */
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  PS8XXX_REG_I2C_DEBUGGING_ENABLE);
	zassert_equal(EC_ERROR_INVAL, ps8xxx_tcpm_drv.init(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on read DCI reg */
	i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
					  PS8XXX_P1_REG_MUX_USB_DCI_CFG);
	zassert_equal(EC_ERROR_INVAL, ps8xxx_tcpm_drv.init(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test successful init */
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);
	check_tcpci_reg(tcpci_emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE,
			PS8XXX_REG_I2C_DEBUGGING_ENABLE_ON);
	zassert_equal(PS8XXX_REG_MUX_USB_DCI_CFG_MODE_OFF,
		      ps8xxx_emul_get_dci_cfg(ps8xxx_emul) &
		      PS8XXX_REG_MUX_USB_DCI_CFG_MODE_MASK, NULL);
}

/** Test PS8815 init */
static void test_ps8815_init(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *p1_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_1);

	/* Set arbitrary FW reg value != 0 for this test */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0x31);
	/* Set correct power status for rest of the test */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS, 0x0);

	/* Test fail on reading HW revision register */
	i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
					  PS8815_P1_REG_HW_REVISION);
	zassert_equal(EC_ERROR_INVAL, ps8xxx_tcpm_drv.init(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test successful init */
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);
}

/** Test PS8xxx release */
static void test_ps8xxx_release(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);
	uint64_t start_ms;

	/* Test successful release with correct FW reg read */
	start_ms = k_uptime_get();
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.release(USBC_PORT_C1),
		      NULL);
	zassert_true(k_uptime_get() - start_ms < 10,
		     "release on correct FW reg read shouldn't wait for chip");

	/* Test delay on FW reg read fail */
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul, PS8XXX_REG_FW_REV);
	start_ms = k_uptime_get();
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.release(USBC_PORT_C1),
		      NULL);
	zassert_true(k_uptime_get() - start_ms >= 10,
		     "release on FW reg read fail should wait for chip");
}

/**
 * Check if PS8815 set_cc write correct value to ROLE_CTRL register and if
 * PS8815 specific workaround is applied to RP_DETECT_CONTROL.
 */
static void check_ps8815_set_cc(enum tcpc_rp_value rp, enum tcpc_cc_pull cc,
				uint16_t rp_detect_ctrl, const char *test_case)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	uint16_t reg_val, exp_role_ctrl;

	/* Clear RP detect register to see if it is set after test */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_RP_DETECT_CONTROL, 0);

	exp_role_ctrl = TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, rp, cc, cc);

	zassert_equal(EC_SUCCESS,
		      ps8xxx_tcpm_drv.select_rp_value(USBC_PORT_C1, rp),
		      "Failed to set RP for case: %s", test_case);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.set_cc(USBC_PORT_C1, cc),
		      "Failed to set CC for case: %s", test_case);

	zassert_ok(tcpci_emul_get_reg(tcpci_emul, TCPC_REG_ROLE_CTRL, &reg_val),
		   "Failed tcpci_emul_get_reg() for case: %s", test_case);
	zassert_equal(exp_role_ctrl, reg_val,
		      "0x%x != (role_ctrl = 0x%x) for case: %s", exp_role_ctrl,
		      reg_val, test_case);
	zassert_ok(tcpci_emul_get_reg(tcpci_emul, PS8XXX_REG_RP_DETECT_CONTROL,
				      &reg_val),
		   "Failed tcpci_emul_get_reg() for case: %s", test_case);
	zassert_equal(rp_detect_ctrl, reg_val,
		      "0x%x != (rp detect = 0x%x) for case: %s", rp_detect_ctrl,
		      reg_val, test_case);
}

/** Test PS8815 set cc and device specific workarounds */
static void test_ps8815_set_cc(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	/* Set firmware version <= 0x10 to set "disable rp detect" workaround */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0x8);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);

	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, RP_DETECT_DISABLE,
			    "fw rev 0x8 \"disable rp detect\" workaround");

	/* First call to set_cc should disarm workaround */
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, 0,
			    "second call without workaround");

	/* drp_toggle should rearm "disable rp detect" workaround */
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.drp_toggle(USBC_PORT_C1),
		      NULL);
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, RP_DETECT_DISABLE,
			    "drp_toggle rearm workaround");

	/*
	 * Set firmware version <= 0x10 to set "disable rp detect" workaround
	 * again
	 */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0xa);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);

	/* CC RD shouldn't trigger "disable rp detect" workaround */
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RD, 0,
			    "CC RD not trigger workaround");

	/*
	 * Set firmware version > 0x10 to unset "disable rp detect"
	 * workaround
	 */
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, 0x12);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);

	/* Firmware > 0x10 shouldn't trigger "disable rp detect" workaround */
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, 0,
			    "fw rev > 0x10 not trigger workaround");

	/*
	 * Set hw revision 0x0a00 to enable workaround for b/171430855 (delay
	 * 1 ms on role control reg update)
	 */
	ps8xxx_emul_set_hw_rev(ps8xxx_emul, 0x0a00);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);

	/*
	 * TODO(b/203858808): Find if it is possible to detect additional 1 ms
	 *                    delay
	 */
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, 0,
			    "delay on HW rev 0x0a00");

	/*
	 * Set hw revision 0x0a01 to enable workaround for b/171430855 (delay
	 * 1 ms on role control reg update)
	 */
	ps8xxx_emul_set_hw_rev(ps8xxx_emul, 0x0a01);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, 0,
			    "delay on HW rev 0x0a01");

	/*
	 * Set other hw revision to disable workaround for b/171430855 (delay
	 * 1 ms on role control reg update)
	 */
	ps8xxx_emul_set_hw_rev(ps8xxx_emul, 0x0a02);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.init(USBC_PORT_C1), NULL);
	check_ps8815_set_cc(TYPEC_RP_1A5, TYPEC_CC_RP, 0,
			    "no delay on other HW rev");
}

/** Test PS8xxx set vconn */
static void test_ps8xxx_set_vconn(void)
{
	uint64_t start_ms;

	/* Test vconn enable */
	start_ms = k_uptime_get();
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.set_vconn(USBC_PORT_C1, 1),
		      NULL);
	zassert_true(k_uptime_get() - start_ms < 10,
		     "VCONN enable should be without delay");

	/* Test vconn disable */
	start_ms = k_uptime_get();
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.set_vconn(USBC_PORT_C1, 0),
		      NULL);
	/* Delay for VCONN disable is required because of issue b/185202064 */
	zassert_true(k_uptime_get() - start_ms >= 10,
		     "VCONN disable require minimum 10ms delay");
}

/** Test PS8xxx transmitting message from TCPC */
static void test_ps8xxx_transmit(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);
	struct tcpci_emul_msg *msg;
	uint64_t exp_cnt, cnt;
	uint16_t reg_val;

	msg = tcpci_emul_get_tx_msg(tcpci_emul);

	/* Test fail on transmitting BIST MODE 2 message */
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul, TCPC_REG_TRANSMIT);
	zassert_equal(EC_ERROR_INVAL,
		      ps8xxx_tcpm_drv.transmit(USBC_PORT_C1,
					       TCPCI_MSG_TX_BIST_MODE_2, 0,
					       NULL), NULL);
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test sending BIST MODE 2 message */
	exp_cnt = PS8751_BIST_COUNTER;
	zassert_equal(EC_SUCCESS,
		      ps8xxx_tcpm_drv.transmit(USBC_PORT_C1,
					       TCPCI_MSG_TX_BIST_MODE_2, 0,
					       NULL), NULL);
	check_tcpci_reg(tcpci_emul, PS8XXX_REG_BIST_CONT_MODE_CTR, 0);
	zassert_equal(TCPCI_MSG_TX_BIST_MODE_2, msg->type, NULL);

	/* Check BIST counter value */
	zassert_ok(tcpci_emul_get_reg(tcpci_emul,
				      PS8XXX_REG_BIST_CONT_MODE_BYTE2,
				      &reg_val), NULL);
	cnt = reg_val;
	cnt <<= 8;
	zassert_ok(tcpci_emul_get_reg(tcpci_emul,
				      PS8XXX_REG_BIST_CONT_MODE_BYTE1,
				      &reg_val), NULL);
	cnt |= reg_val;
	cnt <<= 8;
	zassert_ok(tcpci_emul_get_reg(tcpci_emul,
				      PS8XXX_REG_BIST_CONT_MODE_BYTE0,
				      &reg_val), NULL);
	cnt |= reg_val;
	zassert_equal(exp_cnt, cnt, "0x%llx != 0x%llx", exp_cnt, cnt);
}

/** Test PS8805 and PS8815 drp toggle */
static void test_ps88x5_drp_toggle(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);
	uint16_t exp_role_ctrl;

	/* Test fail on command write */
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL, ps8xxx_tcpm_drv.drp_toggle(USBC_PORT_C1),
		      NULL);

	/* Test fail on role control write */
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul, TCPC_REG_ROLE_CTRL);
	zassert_equal(EC_ERROR_INVAL, ps8xxx_tcpm_drv.drp_toggle(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on CC status read */
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul, TCPC_REG_CC_STATUS);
	zassert_equal(EC_ERROR_INVAL, ps8xxx_tcpm_drv.drp_toggle(USBC_PORT_C1),
		      NULL);
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set CC status as snk, CC lines set arbitrary */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_CC_STATUS,
			   TCPC_REG_CC_STATUS_SET(1, TYPEC_CC_VOLT_OPEN,
						  TYPEC_CC_VOLT_RA));

	/*
	 * TODO(b/203858808): PS8815 sleep here if specific FW rev.
	 *                    Find way to test 1 ms delay
	 */
	/* Test drp toggle when CC is snk. Role control CC lines should be RP */
	exp_role_ctrl = TCPC_REG_ROLE_CTRL_SET(TYPEC_DRP, TYPEC_RP_USB,
					       TYPEC_CC_RP, TYPEC_CC_RP);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.drp_toggle(USBC_PORT_C1),
		      NULL);
	check_tcpci_reg(tcpci_emul, TCPC_REG_ROLE_CTRL, exp_role_ctrl);
	check_tcpci_reg(tcpci_emul, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);

	/* Set CC status as src, CC lines set arbitrary */
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_CC_STATUS,
			   TCPC_REG_CC_STATUS_SET(0, TYPEC_CC_VOLT_OPEN,
						  TYPEC_CC_VOLT_RA));

	/* Test drp toggle when CC is src. Role control CC lines should be RD */
	exp_role_ctrl = TCPC_REG_ROLE_CTRL_SET(TYPEC_DRP, TYPEC_RP_USB,
					       TYPEC_CC_RD, TYPEC_CC_RD);
	zassert_equal(EC_SUCCESS, ps8xxx_tcpm_drv.drp_toggle(USBC_PORT_C1),
		      NULL);
	check_tcpci_reg(tcpci_emul, TCPC_REG_ROLE_CTRL, exp_role_ctrl);
	check_tcpci_reg(tcpci_emul, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);
}

/** Test PS8xxx get chip info code used by all PS8xxx devices */
static void test_ps8xxx_get_chip_info(uint16_t current_product_id)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);
	struct ec_response_pd_chip_info_v1 info;
	uint16_t vendor, product, device_id, fw_rev;

	/* Setup chip info */
	vendor = PS8XXX_VENDOR_ID;
	/* Get currently used product ID */
	product = current_product_id;
	/* Arbitrary choose device ID that doesn't require fixing */
	device_id = 0x2;
	/* Arbitrary revision */
	fw_rev = 0x32;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_PRODUCT_ID, product);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_BCD_DEV, device_id);
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, fw_rev);

	/* Test fail on reading FW revision */
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul, PS8XXX_REG_FW_REV);
	zassert_equal(EC_ERROR_INVAL,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);
	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test reading chip info */
	zassert_equal(EC_SUCCESS,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(device_id, info.device_id, NULL);
	zassert_equal(fw_rev, info.fw_version_number, NULL);

	/* Test fail on wrong vendor id */
	vendor = 0x0;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);
	zassert_equal(EC_ERROR_UNKNOWN,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);

	/* Set correct vendor id */
	vendor = PS8XXX_VENDOR_ID;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);

	/* Set firmware revision to 0 */
	fw_rev = 0x0;
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, fw_rev);

	/*
	 * Test fail on firmware revision equals to 0 when getting chip info
	 * from live device
	 */
	zassert_equal(EC_ERROR_UNKNOWN,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);

	/*
	 * Test if firmware revision 0 is accepted when getting chip info from
	 * not live device
	 */
	zassert_equal(EC_SUCCESS,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 0, &info),
		      NULL);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(device_id, info.device_id, NULL);
	zassert_equal(fw_rev, info.fw_version_number, NULL);

	/* Set wrong vendor id */
	vendor = 0;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);

	/* Test fail on vendor id mismatch on live device */
	zassert_equal(EC_ERROR_UNKNOWN,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);

	/* Test that vendor id is fixed on not live device */
	zassert_equal(EC_SUCCESS,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 0, &info),
		      NULL);
	zassert_equal(PS8XXX_VENDOR_ID, info.vendor_id, NULL);
	zassert_equal(product, info.product_id, NULL);
	zassert_equal(device_id, info.device_id, NULL);
	zassert_equal(fw_rev, info.fw_version_number, NULL);

	/* Set correct vendor id */
	vendor = PS8XXX_VENDOR_ID;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);

	/* Set wrong product id */
	product = 0;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_PRODUCT_ID, product);

	/* Test fail on product id mismatch on live device */
	zassert_equal(EC_ERROR_UNKNOWN,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);

	/* Test that product id is fixed on not live device */
	zassert_equal(EC_SUCCESS,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 0, &info),
		      NULL);
	zassert_equal(vendor, info.vendor_id, NULL);
	zassert_equal(board_get_ps8xxx_product_id(USBC_PORT_C1),
		      info.product_id, NULL);
	zassert_equal(device_id, info.device_id, NULL);
	zassert_equal(fw_rev, info.fw_version_number, NULL);
}

static void test_ps8805_get_chip_info(void)
{
	test_ps8xxx_get_chip_info(PS8805_PRODUCT_ID);
}

static void test_ps8815_get_chip_info(void)
{
	test_ps8xxx_get_chip_info(PS8815_PRODUCT_ID);
}

/** Test PS8805 get chip info and indirectly ps8805_make_device_id */
static void test_ps8805_get_chip_info_fix_dev_id(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *p0_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_0);
	struct ec_response_pd_chip_info_v1 info;
	uint16_t vendor, product, device_id, fw_rev;
	uint16_t chip_rev;

	struct {
		uint16_t exp_dev_id;
		uint16_t chip_rev;
	} test_param[] = {
		/* Test A3 chip revision */
		{
			.exp_dev_id = 0x2,
			.chip_rev = 0xa0,
		},
		/* Test A2 chip revision */
		{
			.exp_dev_id = 0x1,
			.chip_rev = 0x0,
		},
	};

	/* Setup chip info */
	vendor = PS8XXX_VENDOR_ID;
	product = PS8805_PRODUCT_ID;
	/* Arbitrary revision */
	fw_rev = 0x32;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_PRODUCT_ID, product);
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, fw_rev);

	/* Set device id which requires fixing */
	device_id = 0x1;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_BCD_DEV, device_id);

	/* Test error on fixing device id because of fail chip revision read */
	i2c_common_emul_set_read_fail_reg(p0_i2c_emul,
					  PS8805_P0_REG_CHIP_REVISION);
	zassert_equal(EC_ERROR_INVAL,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);
	i2c_common_emul_set_read_fail_reg(p0_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set wrong chip revision */
	chip_rev = 0x32;
	ps8xxx_emul_set_chip_rev(ps8xxx_emul, chip_rev);

	/* Test error on fixing device id */
	zassert_equal(EC_ERROR_UNKNOWN,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);

	/* Test fixing device id for specific chip revisions */
	for (int i = 0; i < ARRAY_SIZE(test_param); i++) {
		ps8xxx_emul_set_chip_rev(ps8xxx_emul, test_param[i].chip_rev);

		/* Test correct device id after fixing */
		zassert_equal(EC_SUCCESS,
			      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1,
							    &info),
			      "Failed to get chip info in test case %d (chip_rev 0x%x)",
			      i, test_param[i].chip_rev);
		zassert_equal(vendor, info.vendor_id,
			      "0x%x != (vendor = 0x%x) in test case %d (chip_rev 0x%x)",
			      vendor, info.vendor_id,
			      i, test_param[i].chip_rev);
		zassert_equal(product, info.product_id,
			      "0x%x != (product = 0x%x) in test case %d (chip_rev 0x%x)",
			      product, info.product_id,
			      i, test_param[i].chip_rev);
		zassert_equal(test_param[i].exp_dev_id, info.device_id,
			      "0x%x != (device = 0x%x) in test case %d (chip_rev 0x%x)",
			      test_param[i].exp_dev_id, info.device_id,
			      i, test_param[i].chip_rev);
		zassert_equal(fw_rev, info.fw_version_number,
			      "0x%x != (FW rev = 0x%x) in test case %d (chip_rev 0x%x)",
			      fw_rev, info.fw_version_number,
			      i, test_param[i].chip_rev);
	}
}

/** Test PS8815 get chip info and indirectly ps8815_make_device_id */
static void test_ps8815_get_chip_info_fix_dev_id(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *p1_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_1);
	struct ec_response_pd_chip_info_v1 info;
	uint16_t vendor, product, device_id, fw_rev;
	uint16_t hw_rev;

	struct {
		uint16_t exp_dev_id;
		uint16_t hw_rev;
	} test_param[] = {
		/* Test A0 HW revision */
		{
			.exp_dev_id = 0x1,
			.hw_rev = 0x0a00,
		},
		/* Test A1 HW revision */
		{
			.exp_dev_id = 0x2,
			.hw_rev = 0x0a01,
		},
		/* Test A2 HW revision */
		{
			.exp_dev_id = 0x3,
			.hw_rev = 0x0a02,
		},
	};

	/* Setup chip info */
	vendor = PS8XXX_VENDOR_ID;
	product = PS8815_PRODUCT_ID;
	/* Arbitrary revision */
	fw_rev = 0x32;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_VENDOR_ID, vendor);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_PRODUCT_ID, product);
	tcpci_emul_set_reg(tcpci_emul, PS8XXX_REG_FW_REV, fw_rev);

	/* Set device id which requires fixing */
	device_id = 0x1;
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_BCD_DEV, device_id);

	/* Test error on fixing device id because of fail hw revision read */
	i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
					  PS8815_P1_REG_HW_REVISION);
	zassert_equal(EC_ERROR_INVAL,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);
	i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set wrong hw revision */
	hw_rev = 0x32;
	ps8xxx_emul_set_hw_rev(ps8xxx_emul, hw_rev);

	/* Test error on fixing device id */
	zassert_equal(EC_ERROR_UNKNOWN,
		      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1, &info),
		      NULL);

	/* Test fixing device id for specific HW revisions */
	for (int i = 0; i < ARRAY_SIZE(test_param); i++) {
		ps8xxx_emul_set_hw_rev(ps8xxx_emul, test_param[i].hw_rev);

		/* Test correct device id after fixing */
		zassert_equal(EC_SUCCESS,
			      ps8xxx_tcpm_drv.get_chip_info(USBC_PORT_C1, 1,
							    &info),
			      "Failed to get chip info in test case %d (hw_rev 0x%x)",
			      i, test_param[i].hw_rev);
		zassert_equal(vendor, info.vendor_id,
			      "0x%x != (vendor = 0x%x) in test case %d (hw_rev 0x%x)",
			      vendor, info.vendor_id, i, test_param[i].hw_rev);
		zassert_equal(product, info.product_id,
			      "0x%x != (product = 0x%x) in test case %d (hw_rev 0x%x)",
			      product, info.product_id,
			      i, test_param[i].hw_rev);
		zassert_equal(test_param[i].exp_dev_id, info.device_id,
			      "0x%x != (device = 0x%x) in test case %d (hw_rev 0x%x)",
			      test_param[i].exp_dev_id, info.device_id,
			      i, test_param[i].hw_rev);
		zassert_equal(fw_rev, info.fw_version_number,
			      "0x%x != (FW rev = 0x%x) in test case %d (hw_rev 0x%x)",
			      fw_rev, info.fw_version_number,
			      i, test_param[i].hw_rev);
	}
}

/** Test PS8805 get/set gpio */
static void test_ps8805_gpio(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	struct i2c_emul *gpio_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_GPIO);
	uint8_t exp_ctrl, gpio_ctrl;
	int level;

	struct {
		enum ps8805_gpio signal;
		uint16_t gpio_reg;
		int level;
	} test_param[] = {
		/* Chain of set and unset GPIO to test */
		{
			.gpio_reg = PS8805_REG_GPIO_0,
			.signal = PS8805_GPIO_0,
			.level = 1,
		},
		{
			.gpio_reg = PS8805_REG_GPIO_1,
			.signal = PS8805_GPIO_1,
			.level = 1,
		},
		{
			.gpio_reg = PS8805_REG_GPIO_2,
			.signal = PS8805_GPIO_2,
			.level = 1,
		},
		/* Test setting GPIO 0 which is already set */
		{
			.gpio_reg = PS8805_REG_GPIO_0,
			.signal = PS8805_GPIO_0,
			.level = 1,
		},
		/* Test clearing GPIOs */
		{
			.gpio_reg = PS8805_REG_GPIO_0,
			.signal = PS8805_GPIO_0,
			.level = 0,
		},
		{
			.gpio_reg = PS8805_REG_GPIO_1,
			.signal = PS8805_GPIO_1,
			.level = 0,
		},
		{
			.gpio_reg = PS8805_REG_GPIO_2,
			.signal = PS8805_GPIO_2,
			.level = 0,
		},
		/* Test clearing GPIO 0 which is already unset */
		{
			.gpio_reg = PS8805_REG_GPIO_0,
			.signal = PS8805_GPIO_0,
			.level = 1,
		},
	};

	/* Setup fail on gpio control reg read */
	i2c_common_emul_set_read_fail_reg(gpio_i2c_emul,
					  PS8805_REG_GPIO_CONTROL);

	/* Test fail on reading gpio control reg */
	zassert_equal(EC_ERROR_INVAL,
		      ps8805_gpio_set_level(USBC_PORT_C1, PS8805_GPIO_0, 1),
		      NULL);
	zassert_equal(EC_ERROR_INVAL,
		      ps8805_gpio_get_level(USBC_PORT_C1, PS8805_GPIO_0,
					    &level), NULL);

	/* Do not fail on gpio control reg read */
	i2c_common_emul_set_read_fail_reg(gpio_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on writing gpio control reg */
	i2c_common_emul_set_write_fail_reg(gpio_i2c_emul,
					   PS8805_REG_GPIO_CONTROL);
	zassert_equal(EC_ERROR_INVAL,
		      ps8805_gpio_set_level(USBC_PORT_C1, PS8805_GPIO_0, 1),
		      NULL);
	i2c_common_emul_set_write_fail_reg(gpio_i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Clear gpio control reg */
	ps8xxx_emul_set_gpio_ctrl(ps8xxx_emul, 0x0);
	exp_ctrl = 0;

	/* Test set and unset GPIO */
	for (int i = 0; i < ARRAY_SIZE(test_param); i++) {
		if (test_param[i].level) {
			exp_ctrl |= test_param[i].gpio_reg;
		} else {
			exp_ctrl &= ~test_param[i].gpio_reg;
		}
		zassert_equal(EC_SUCCESS,
			      ps8805_gpio_set_level(USBC_PORT_C1,
						    test_param[i].signal,
						    test_param[i].level),
			      "Failed gpio_set in test case %d (gpio %d, level %d)",
			      i, test_param[i].signal, test_param[i].level);
		zassert_equal(EC_SUCCESS,
			      ps8805_gpio_get_level(USBC_PORT_C1,
						    test_param[i].signal,
						    &level),
			      "Failed gpio_get in test case %d (gpio %d, level %d)",
			      i, test_param[i].signal, test_param[i].level);
		zassert_equal(test_param[i].level, level,
			      "%d != (gpio_get_level = %d) in test case %d (gpio %d, level %d)",
			      test_param[i].level, level, i,
			      test_param[i].signal, test_param[i].level);
		gpio_ctrl = ps8xxx_emul_get_gpio_ctrl(ps8xxx_emul);
		zassert_equal(exp_ctrl, gpio_ctrl,
			      "0x%x != (gpio_ctrl = 0x%x) in test case %d (gpio %d, level %d)",
			      exp_ctrl, gpio_ctrl, i, test_param[i].signal,
			      test_param[i].level);
	}
}

/** Test TCPCI init and vbus level */
static void test_ps8xxx_tcpci_init(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_init(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI release */
static void test_ps8xxx_tcpci_release(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_release(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI get cc */
static void test_ps8xxx_tcpci_get_cc(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_get_cc(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI set cc */
static void test_ps8xxx_tcpci_set_cc(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_set_cc(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI set polarity */
static void test_ps8xxx_tcpci_set_polarity(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_set_polarity(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI set vconn */
static void test_ps8xxx_tcpci_set_vconn(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_set_vconn(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI set msg header */
static void test_ps8xxx_tcpci_set_msg_header(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_set_msg_header(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI get raw message */
static void test_ps8xxx_tcpci_get_rx_message_raw(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_get_rx_message_raw(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI transmitting message */
static void test_ps8xxx_tcpci_transmit(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_transmit(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI alert */
static void test_ps8xxx_tcpci_alert(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_alert(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI alert RX message */
static void test_ps8xxx_tcpci_alert_rx_message(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_alert_rx_message(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI enter low power mode */
static void test_ps8xxx_tcpci_low_power_mode(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	/*
	 * PS8751/PS8815 has the auto sleep function that enters
	 * low power mode on its own in ~2 seconds. Other chips
	 * don't have it. Stub it out for PS8751/PS8815.
	 */
	if (board_get_ps8xxx_product_id(USBC_PORT_C1) == PS8751_PRODUCT_ID ||
		board_get_ps8xxx_product_id(USBC_PORT_C1) == PS8815_PRODUCT_ID)
		return;
	test_tcpci_low_power_mode(tcpci_emul, USBC_PORT_C1);
}

/** Test TCPCI set bist test mode */
static void test_ps8xxx_tcpci_set_bist_mode(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);

	test_tcpci_set_bist_mode(tcpci_emul, USBC_PORT_C1);
}

/* Setup no fail for all I2C devices associated with PS8xxx emulator */
static void setup_no_fail_all(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);
	struct i2c_emul *p0_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_0);
	struct i2c_emul *p1_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_1);
	struct i2c_emul *gpio_i2c_emul =
		ps8xxx_emul_get_i2c_emul(ps8xxx_emul, PS8XXX_EMUL_PORT_GPIO);

	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	if (p0_i2c_emul != NULL) {
		i2c_common_emul_set_read_fail_reg(p0_i2c_emul,
						  I2C_COMMON_EMUL_NO_FAIL_REG);
		i2c_common_emul_set_write_fail_reg(p0_i2c_emul,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}

	if (p1_i2c_emul != NULL) {
		i2c_common_emul_set_read_fail_reg(p1_i2c_emul,
						  I2C_COMMON_EMUL_NO_FAIL_REG);
		i2c_common_emul_set_write_fail_reg(p1_i2c_emul,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}

	if (gpio_i2c_emul != NULL) {
		i2c_common_emul_set_read_fail_reg(gpio_i2c_emul,
						  I2C_COMMON_EMUL_NO_FAIL_REG);
		i2c_common_emul_set_write_fail_reg(gpio_i2c_emul,
						   I2C_COMMON_EMUL_NO_FAIL_REG);
	}
}

/**
 * Setup PS8xxx emulator to mimic PS8805 and setup no fail for all I2C devices
 * associated with PS8xxx emulator
 */
static void setup_ps8805(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);

	board_set_ps8xxx_product_id(PS8805_PRODUCT_ID);
	ps8xxx_emul_set_product_id(ps8xxx_emul, PS8805_PRODUCT_ID);
	setup_no_fail_all();
}

/**
 * Setup PS8xxx emulator to mimic PS8815 and setup no fail for all I2C devices
 * associated with PS8xxx emulator
 */
static void setup_ps8815(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);

	board_set_ps8xxx_product_id(PS8815_PRODUCT_ID);
	ps8xxx_emul_set_product_id(ps8xxx_emul, PS8815_PRODUCT_ID);
	setup_no_fail_all();
}

void test_suite_ps8xxx(void)
{
	ztest_test_suite(ps8805,
			 ztest_unit_test_setup_teardown(test_ps8xxx_init_fail,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8805_init,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_release,
				 setup_no_fail_all, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_set_vconn,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_transmit,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps88x5_drp_toggle,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8805_get_chip_info,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8805_get_chip_info_fix_dev_id,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8805_gpio,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_tcpci_init,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_release,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_get_cc,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_cc,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_polarity,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_vconn,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_msg_header,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_get_rx_message_raw,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_transmit,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_alert,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_alert_rx_message,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_low_power_mode,
				 setup_ps8805, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_bist_mode,
				 setup_ps8805, unit_test_noop));
	ztest_run_test_suite(ps8805);

	ztest_test_suite(ps8815,
			 ztest_unit_test_setup_teardown(test_ps8xxx_init_fail,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8815_init,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_release,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8815_set_cc,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_set_vconn,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_transmit,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps88x5_drp_toggle,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8815_get_chip_info,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8815_get_chip_info_fix_dev_id,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_tcpci_init,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_release,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_get_cc,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_cc,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_polarity,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_vconn,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_msg_header,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_get_rx_message_raw,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_transmit,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_alert,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_alert_rx_message,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_low_power_mode,
				 setup_ps8815, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8xxx_tcpci_set_bist_mode,
				 setup_ps8815, unit_test_noop));
	ztest_run_test_suite(ps8815);
}
