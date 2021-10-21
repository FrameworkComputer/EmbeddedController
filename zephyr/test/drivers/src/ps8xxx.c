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

/* Setup no fail for all I2C devices associated with PS8xxx emulator */
static void setup_no_fail_all(void)
{
	const struct emul *ps8xxx_emul = emul_get_binding(PS8XXX_EMUL_LABEL);
	const struct emul *tcpci_emul = ps8xxx_emul_get_tcpci(ps8xxx_emul);
	struct i2c_emul *tcpci_i2c_emul = tcpci_emul_get_i2c_emul(tcpci_emul);

	i2c_common_emul_set_read_fail_reg(tcpci_i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(tcpci_i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

void test_suite_ps8xxx(void)
{
	ztest_test_suite(ps8805,
			 ztest_unit_test_setup_teardown(test_ps8xxx_init_fail,
				 setup_no_fail_all, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_release,
				 setup_no_fail_all, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_set_vconn,
				 setup_no_fail_all, unit_test_noop),
			 ztest_unit_test_setup_teardown(test_ps8xxx_transmit,
				 setup_no_fail_all, unit_test_noop),
			 ztest_unit_test_setup_teardown(
				 test_ps8805_get_chip_info,
				 setup_no_fail_all, unit_test_noop));
	ztest_run_test_suite(ps8805);
}
