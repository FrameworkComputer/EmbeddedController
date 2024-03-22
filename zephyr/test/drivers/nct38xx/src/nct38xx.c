/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "emul/tcpc/emul_nct38xx.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define ALERT_TEST_VAL 0xffff

#define NCT38XX_PORT 0
#define NCT38XX_EMUL EMUL_GET_CHIP_BINDING(DT_NODELABEL(nct38xx_emul))

int nct38xx_tcpm_init(int port);
int nct38xx_tcpm_set_snk_ctrl(int port, int enable);
int nct38xx_set_frs_enable(int port, int enable);
int nct3807_handle_fault(int port, int fault);
int nct38xx_tcpm_set_cc(int port, int pull);

/* Helper functions to make tests cleaner. */
static int nct38xx_emul_test_get_reg(int r, uint16_t *val)
{
	return nct38xx_emul_get_reg(NCT38XX_EMUL, r, val);
}

static int nct38xx_emul_test_set_reg(int r, uint16_t val)
{
	return nct38xx_emul_set_reg(NCT38XX_EMUL, r, val);
}

static void nct38xx_test_reset(void *fixture)
{
	ARG_UNUSED(fixture);

	nct38xx_reset_notify(NCT38XX_PORT);
}

ZTEST_SUITE(nct38xx, NULL, NULL, nct38xx_test_reset, NULL, NULL);

/* Validate reading and writing emulator registers. */
ZTEST(nct38xx, test_emul_registers_rw)
{
	int rv;
	uint8_t expected;
	uint16_t val;

	/* Check vendor-defined registers. */
	expected = (uint8_t)(NCT38XX_REG_CTRL_OUT_EN_RESERVED_MASK &
			     NCT38XX_REG_CTRL_OUT_EN_DEFAULT);
	expected |= (uint8_t)(~NCT38XX_REG_CTRL_OUT_EN_RESERVED_MASK);
	rv = nct38xx_emul_test_set_reg(NCT38XX_REG_CTRL_OUT_EN, expected);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_equal(val, expected);

	expected = (uint8_t)(NCT38XX_REG_VBC_FAULT_CTL_RESERVED_MASK &
			     NCT38XX_REG_VBC_FAULT_CTL_DEFAULT);
	expected |= (uint8_t)(~NCT38XX_REG_VBC_FAULT_CTL_RESERVED_MASK);
	rv = nct38xx_emul_test_set_reg(NCT38XX_REG_VBC_FAULT_CTL, expected);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_VBC_FAULT_CTL, &val);
	zassert_ok(rv);
	zassert_equal(val, expected);

	/*
	 * Check that access to general TCPC registers passes through the
	 * underlying TCPC emulator.
	 */
	rv = nct38xx_emul_test_set_reg(TCPC_REG_ALERT, ALERT_TEST_VAL);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(TCPC_REG_ALERT, &val);
	zassert_ok(rv);
	zassert_equal(val, ALERT_TEST_VAL);
}

/* Validate that changing reserved bits fails. */
ZTEST(nct38xx, test_emul_registers_reserved)
{
	int rv;
	uint8_t expected;

	expected = (uint8_t)(NCT38XX_REG_CTRL_OUT_EN_RESERVED_MASK &
			     NCT38XX_REG_CTRL_OUT_EN_DEFAULT);
	expected |= (uint8_t)(~NCT38XX_REG_CTRL_OUT_EN_RESERVED_MASK);
	rv = nct38xx_emul_test_set_reg(NCT38XX_REG_CTRL_OUT_EN, ~expected);
	zassert_true(rv);

	expected = (uint8_t)(NCT38XX_REG_VBC_FAULT_CTL_RESERVED_MASK &
			     NCT38XX_REG_VBC_FAULT_CTL_DEFAULT);
	expected |= (uint8_t)(~NCT38XX_REG_VBC_FAULT_CTL_RESERVED_MASK);
	rv = nct38xx_emul_test_set_reg(NCT38XX_REG_VBC_FAULT_CTL, ~expected);
	zassert_true(rv);
}

/* Validate the emulator's reset function. */
ZTEST(nct38xx, test_emul_reset)
{
	int rv;
	uint16_t val;

	/*
	 * Clear the fault status register then check if the registers reset
	 * flag is set.
	 */
	nct38xx_emul_test_set_reg(TCPC_REG_FAULT_STATUS, 0);
	nct38xx_emul_reset(NCT38XX_EMUL);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_FAULT_STATUS, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_FAULT_STATUS_ALL_REGS_RESET);

	/* Test vendor-specific registers. */
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_equal(val, NCT38XX_REG_CTRL_OUT_EN_DEFAULT);

	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_VBC_FAULT_CTL, &val);
	zassert_ok(rv);
	zassert_equal(val, NCT38XX_REG_VBC_FAULT_CTL_DEFAULT);
}

static void validate_init(void)
{
	int rv;
	uint16_t val;

	/* Validate REG_CTRL_OUT_EN flags.*/
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & (NCT38XX_REG_CTRL_OUT_EN_SRCEN |
			    NCT38XX_REG_CTRL_OUT_EN_SNKEN |
			    NCT38XX_REG_CTRL_OUT_EN_CONNDIREN));

	/* Validate that OVP is disabled. */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_FAULT_CTRL, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS);

	/* Validate that VBus monitor is enabled and FRS is disabled. */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_POWER_CTRL, &val);
	zassert_ok(rv);
	zassert_false(val & (TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
			     TCPC_REG_POWER_CTRL_FRS_ENABLE));

	/* Validate FRS direction. */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_CONFIG_EXT_1, &val);
	zassert_ok(rv);
	zassert_equal(val, TCPC_REG_CONFIG_EXT_1_FR_SWAP_SNK_DIR);

	/* Validate alert config. */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_ALERT_MASK, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_ALERT_FAULT);

	/* Validate VBC_FAULT_CTL flags. */
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_VBC_FAULT_CTL, &val);
	zassert_ok(rv);
	zassert_true(val & (NCT38XX_REG_VBC_FAULT_CTL_VC_OCP_EN |
			    NCT38XX_REG_VBC_FAULT_CTL_VC_SCP_EN |
			    NCT38XX_REG_VBC_FAULT_CTL_FAULT_VC_OFF));
}

/* Tests nct38xx_tcpm_init from a non-dead battery. */
ZTEST(nct38xx, test_init)
{
	int rv;
	uint16_t val;

	rv = nct38xx_emul_test_set_reg(TCPC_REG_ROLE_CTRL,
				       NCT38XX_ROLE_CTRL_GOOD_BATTERY);
	zassert_ok(rv);

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);
	zassert_equal(nct38xx_get_boot_type(NCT38XX_PORT), NCT38XX_BOOT_NORMAL);

	/*
	 * TCPC_CONTROL.DebugAccessoryControl should be enabled after a normal
	 * boot.
	 */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_TCPC_CTRL, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL);

	validate_init();
}

/* Tests nct38xx_tcpm_init from a dead battery with a debug accessory. */
ZTEST(nct38xx, test_dead_init_accessory)
{
	int rv;
	uint16_t val;

	/*
	 * TCPC_CONTROL.DebugAccessoryControl should stay enabled after a dead
	 * battery boot.
	 */
	rv = nct38xx_emul_test_set_reg(TCPC_REG_ROLE_CTRL,
				       NCT38XX_ROLE_CTRL_DEAD_BATTERY);
	zassert_ok(rv);

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);
	zassert_equal(nct38xx_get_boot_type(NCT38XX_PORT),
		      NCT38XX_BOOT_DEAD_BATTERY);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_TCPC_CTRL, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL);

	validate_init();
}

/* Tests nct38xx_tcpm_init from a dead battery without a debug accessory. */
ZTEST(nct38xx, test_dead_init_no_accessory)
{
	int rv;
	uint16_t val;

	/*
	 * TCPC_CONTROL.DebugAccessoryControl should be enabled after a dead
	 * battery boot if it wasn't.
	 */
	rv = nct38xx_emul_test_set_reg(TCPC_REG_ROLE_CTRL,
				       NCT38XX_ROLE_CTRL_DEAD_BATTERY);
	zassert_ok(rv);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_TCPC_CTRL, &val);
	zassert_ok(rv);
	rv = nct38xx_emul_test_set_reg(
		TCPC_REG_TCPC_CTRL,
		val & ~TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL);
	zassert_ok(rv);

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);
	zassert_equal(nct38xx_get_boot_type(NCT38XX_PORT),
		      NCT38XX_BOOT_DEAD_BATTERY);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_TCPC_CTRL, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL);

	validate_init();
}

/* Test nct38xx_reset_notify. */
ZTEST(nct38xx, test_reset_notify)
{
	int rv;

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);

	nct38xx_reset_notify(NCT38XX_PORT);
	zassert_equal(nct38xx_get_boot_type(NCT38XX_PORT),
		      NCT38XX_BOOT_UNKNOWN);
}

/* Test nct38xx_tcpm_set_snk_ctrl. */
ZTEST(nct38xx, test_tcpm_set_snk_ctrl)
{
	int rv;
	uint16_t val;

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);

	/* Validate disable */
	rv = nct38xx_tcpm_set_snk_ctrl(NCT38XX_PORT, 0);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);
	zassert_false(tcpci_tcpm_get_snk_ctrl(NCT38XX_PORT));

	/* Enable shouldn't change the SNKEN bit. Check with bit cleared. */
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	rv = nct38xx_emul_test_set_reg(NCT38XX_REG_CTRL_OUT_EN,
				       val | NCT38XX_REG_CTRL_OUT_EN_SNKEN);
	zassert_ok(rv);

	rv = nct38xx_tcpm_set_snk_ctrl(NCT38XX_PORT, 1);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);
	zassert_true(tcpci_tcpm_get_snk_ctrl(NCT38XX_PORT));

	/* Verify that the SNKEN bit doesn't get cleared. */
	rv = nct38xx_tcpm_set_snk_ctrl(NCT38XX_PORT, 1);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);
	zassert_true(tcpci_tcpm_get_snk_ctrl(NCT38XX_PORT));
}

/* Test nct38xx_tcpm_set_cc. */
ZTEST(nct38xx, test_tcpm_set_cc)
{
	int rv;
	uint16_t val;

	/*
	 * Test with sinking enabled. Only TYPEC_CC_OPEN should result in
	 * snken being disabled
	 */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_POWER_STATUS, &val);
	zassert_ok(rv);
	rv = nct38xx_emul_test_set_reg(
		TCPC_REG_POWER_STATUS,
		val | TCPC_REG_POWER_STATUS_SINKING_VBUS);
	zassert_ok(rv);

	/* Test that tcpci_tcpm_set_cc is being called. */
	/* CC Open */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_OPEN);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_false(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_ROLE_CTRL, &val);
	zassert_ok(rv);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_OPEN);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_OPEN);

	/* CC RA */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RA);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_ROLE_CTRL, &val);
	zassert_ok(rv);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RA);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RA);

	/* CC RP */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RP);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_ROLE_CTRL, &val);
	zassert_ok(rv);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RP);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RP);

	/* CC RD */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RD);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_ROLE_CTRL, &val);
	zassert_ok(rv);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RD);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RD);

	/* CC RA RD */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RA_RD);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_ROLE_CTRL, &val);
	zassert_ok(rv);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RA);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RA);

	/*
	 * Test with sinking disabled. NCT38XX_REG_CTRL_OUT_EN_SNKEN
	 * should always be set.
	 */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_POWER_STATUS, &val);
	zassert_ok(rv);
	rv = nct38xx_emul_test_set_reg(
		TCPC_REG_POWER_STATUS,
		val & ~TCPC_REG_POWER_STATUS_SINKING_VBUS);
	zassert_ok(rv);

	/* Test that tcpci_tcpm_set_cc is being called. */
	/* CC Open */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_OPEN);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_ROLE_CTRL, &val);
	zassert_ok(rv);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_OPEN);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_OPEN);

	/* CC RA */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RA);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RA);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RP);

	/* CC RP */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RP);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RA);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RP);

	/* CC RD */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RD);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	/* CC RA RD */
	rv = nct38xx_tcpm_set_cc(NCT38XX_PORT, TYPEC_CC_RA_RD);
	zassert_ok(rv);
	rv = nct38xx_emul_test_get_reg(NCT38XX_REG_CTRL_OUT_EN, &val);
	zassert_ok(rv);
	zassert_true(val & NCT38XX_REG_CTRL_OUT_EN_SNKEN);

	zassert_equal(TCPC_REG_ROLE_CTRL_CC1(val), TYPEC_CC_RA);
	zassert_equal(TCPC_REG_ROLE_CTRL_CC2(val), TYPEC_CC_RP);
}

/* Test nct38xx_set_frs_enable. */
ZTEST(nct38xx, test_set_frs_enable)
{
	int rv;
	uint16_t val;

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);

	/* Test disable */
	rv = nct38xx_set_frs_enable(NCT38XX_PORT, 0);
	zassert_ok(rv);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_VBUS_SINK_DISCONNECT_THRESH,
				       &val);
	zassert_ok(rv);
	zassert_equal(val, TCPC_REG_VBUS_SINK_DISCONNECT_THRESH_DEFAULT);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_POWER_CTRL, &val);
	zassert_ok(rv);
	zassert_false(val & TCPC_REG_POWER_CTRL_FRS_ENABLE);

	/* Test enable */
	rv = nct38xx_set_frs_enable(NCT38XX_PORT, 1);
	zassert_ok(rv);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_VBUS_SINK_DISCONNECT_THRESH,
				       &val);
	zassert_ok(rv);
	zassert_equal(val, 0);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_POWER_CTRL, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_POWER_CTRL_FRS_ENABLE);
}

ZTEST(nct38xx, test_nct3807_handle_fault)
{
	uint16_t val;
	int rv;

	/* Verify that the nct38xx init function gets called. */
	rv = nct3807_handle_fault(NCT38XX_PORT,
				  TCPC_REG_FAULT_STATUS_ALL_REGS_RESET);
	zassert_ok(rv);
	validate_init();

	/* Verify that OVP gets disabled. */
	rv = nct38xx_emul_test_get_reg(TCPC_REG_FAULT_CTRL, &val);
	zassert_ok(rv);
	rv = nct38xx_emul_test_set_reg(
		TCPC_REG_FAULT_CTRL,
		val & ~TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS);
	zassert_ok(rv);

	rv = nct3807_handle_fault(NCT38XX_PORT,
				  TCPC_REG_FAULT_STATUS_VBUS_OVER_VOLTAGE);
	zassert_ok(rv);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_FAULT_CTRL, &val);
	zassert_ok(rv);
	zassert_true(val & TCPC_REG_FAULT_CTRL_VBUS_OVP_FAULT_DIS);

	/* Test auto discharge failure. */
	rv = nct38xx_emul_test_get_reg(
		TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT, &val);
	zassert_ok(rv);
	rv = nct38xx_emul_test_set_reg(
		TCPC_REG_FAULT_CTRL,
		val & TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT);
	zassert_ok(rv);

	rv = nct3807_handle_fault(NCT38XX_PORT,
				  TCPC_REG_FAULT_STATUS_AUTO_DISCHARGE_FAIL);
	zassert_ok(rv);

	rv = nct38xx_emul_test_get_reg(TCPC_REG_POWER_CTRL, &val);
	zassert_ok(rv);
	zassert_false(val & TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT);
}

/* Test nct38xx_lock. */
ZTEST(nct38xx, test_mfd_lock)
{
	int rv;
	uint16_t val;
	uint8_t reg;

	rv = nct38xx_tcpm_init(NCT38XX_PORT);
	zassert_ok(rv);

	/* Perform a tcpc_xfer(), which utilizes the multi function device
	 * locking. This is an indirect test of the locking as there are no
	 * side effects that we can check to confirm the lock was obtained.
	 */

	reg = TCPC_REG_ALERT_MASK;

	/* Test reading value using tcpc_xfer() function. We only need
	 * verify that the operation completed, the data returned is not
	 * relevant to the test.
	 */
	zassert_equal(EC_SUCCESS,
		      tcpc_xfer(NCT38XX_PORT, &reg, 1, (uint8_t *)&val, 2),
		      NULL);
}
