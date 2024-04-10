/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "driver/retimer/bb_retimer.h"
#include "ec_tasks.h"
#include "emul/emul_bb_retimer.h"
#include "emul/emul_common_i2c.h"
#include "hooks.h"
#include "i2c.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_dp_alt_mode.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define GPIO_USB_C1_LS_EN_PATH NAMED_GPIOS_GPIO_NODE(usb_c1_ls_en)
#define GPIO_USB_C1_LS_EN_PORT DT_GPIO_PIN(GPIO_USB_C1_LS_EN_PATH, gpios)
#define GPIO_USB_C1_RT_RST_ODL_PATH NAMED_GPIOS_GPIO_NODE(usb_c1_rt_rst_odl)
#define GPIO_USB_C1_RT_RST_ODL_PORT \
	DT_GPIO_PIN(GPIO_USB_C1_RT_RST_ODL_PATH, gpios)
#define BB_RETIMER_NODE DT_NODELABEL(usb_c1_bb_retimer_emul)

/** Test is retimer fw update capable function. */
ZTEST_USER(bb_retimer, test_bb_is_fw_update_capable)
{
	/* BB retimer is fw update capable */
	zassert_true(bb_usb_retimer.is_retimer_fw_update_capable());
}

/** Test is retimer fw update capable function. */
ZTEST_USER(bb_retimer_no_tasks, test_bb_set_state)
{
	struct pd_discovery *disc;
	uint32_t conn, exp_conn;
	const struct emul *emul = EMUL_DT_GET(BB_RETIMER_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bb_retimer_get_i2c_common_data(emul);
	bool ack_required;

	set_test_runner_tid();

	/* Setup emulator fail on write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   BB_RETIMER_REG_CONNECTION_STATE);

	/* Test fail on reset register write */
	zassert_equal(EC_ERROR_INVAL,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_NONE, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");

	/* Do not fail on write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set UFP role for whole test */
	tc_set_data_role(USBC_PORT_C1, PD_ROLE_UFP);
	zassert_equal(PD_ROLE_UFP, pd_get_data_role(USBC_PORT_C1));

	/* Test none mode */
	bb_emul_set_reg(emul, BB_RETIMER_REG_CONNECTION_STATE, 0x12144678);
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_NONE, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	/* Only UFP mode is set */
	exp_conn = BB_RETIMER_USB_DATA_ROLE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test USB3 gen1 mode */
	prl_set_rev(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME, PD_REV10);
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_USB_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_USB_3_CONNECTION;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test USB3 gen2 mode */
	disc = pd_get_am_discovery_and_notify_access(USBC_PORT_C1,
						     TCPCI_MSG_SOP_PRIME);
	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN2;
	prl_set_rev(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME, PD_REV30);
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_USB_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_USB_3_CONNECTION | BB_RETIMER_USB_3_SPEED;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test USB4 mode */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_USB4_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT | BB_RETIMER_USB4_ENABLED;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test USB4 mode with polarity inverted */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_USB4_ENABLED |
						 USB_PD_MUX_POLARITY_INVERTED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_CONNECTION_ORIENTATION | BB_RETIMER_USB4_ENABLED;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test DP mode */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED |
						 USB_PD_MUX_HPD_IRQ,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION | BB_RETIMER_IRQ_HPD;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED |
						 USB_PD_MUX_HPD_LVL,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_USB_DATA_ROLE |
		   BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION | BB_RETIMER_HPD_LVL;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);
}

/** Test retimer idle mode setting. */
ZTEST_USER(bb_retimer_no_tasks, test_bb_set_idle_mode)
{
	const uint32_t usb3_conn =
		BB_RETIMER_ACTIVE_PASSIVE | BB_RETIMER_USB_3_SPEED |
		BB_RETIMER_USB_3_CONNECTION | BB_RETIMER_RE_TIMER_DRIVER |
		BB_RETIMER_DATA_CONNECTION_PRESENT;
	const uint32_t idle_conn =
		BB_RETIMER_ACTIVE_PASSIVE | BB_RETIMER_USB_3_SPEED |
		BB_RETIMER_RE_TIMER_DRIVER | BB_RETIMER_DATA_CONNECTION_PRESENT;
	const struct emul *emul = EMUL_DT_GET(BB_RETIMER_NODE);
	struct usb_mux usb_mux_c1;
	uint32_t conn;
	bool ack_required;

	set_test_runner_tid();

	/* Enable IDLE mode port C1 mux */
	usb_mux_c1 = *usb_muxes[USBC_PORT_C1].mux;
	usb_mux_c1.flags |= USB_MUX_FLAG_CAN_IDLE;

	/* Check if USB3 is enabled before idle entry */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(&usb_mux_c1, USB_PD_MUX_USB_ENABLED,
					 &ack_required),
		      NULL);
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	zassert_equal(conn, usb3_conn, "Expected state 0x%02x, got 0x%02x",
		      usb3_conn, conn);

	/* Check if USB3 is disabled on idle entry */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set_idle_mode(&usb_mux_c1, true), NULL);
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	zassert_equal(conn, idle_conn, "Expected state 0x%02x, got 0x%02x",
		      idle_conn, conn);

	/* Check if USB3 is re-enabled on idle exit */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set_idle_mode(&usb_mux_c1, false), NULL);
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	zassert_equal(conn, usb3_conn, "Expected state 0x%02x, got 0x%02x",
		      usb3_conn, conn);
}

/** Test retimer dp connection setting. */
ZTEST_USER(bb_retimer_no_tasks, test_bb_retimer_set_dp_connection)
{
	const uint32_t enable_dp_conn = BB_RETIMER_DP_CONNECTION;
	const uint32_t disable_dp_conn = 0;
	const struct emul *emul = EMUL_DT_GET(BB_RETIMER_NODE);
	struct usb_mux usb_mux_c1;
	uint32_t conn;

	set_test_runner_tid();

	usb_mux_c1 = *usb_muxes[USBC_PORT_C1].mux;

	/* Check if DP is enabled */
	zassert_equal(EC_SUCCESS,
		      bb_retimer_set_dp_connection(&usb_mux_c1, true), NULL);
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	zassert_equal(conn, enable_dp_conn, "Expected state 0x%02x, got 0x%02x",
		      enable_dp_conn, conn);

	/* Check if DP is disabled */
	zassert_equal(EC_SUCCESS,
		      bb_retimer_set_dp_connection(&usb_mux_c1, false), NULL);
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	zassert_equal(conn, disable_dp_conn,
		      "Expected state 0x%02x, got 0x%02x", disable_dp_conn,
		      conn);
}

#if defined(EC_USB_PD_DP21_MODE)
ZTEST_USER(bb_retimer_no_tasks, test_bb_set_dfp_dp_21_cable)
{
	union dp_mode_resp_cable cable_resp;
	union dp_mode_cfg device_resp;
	struct pd_discovery *disc, *dev_disc;
	uint32_t conn, exp_conn;
	const struct emul *emul = EMUL_DT_GET(BB_RETIMER_NODE);
	bool ack_required;

	set_test_runner_tid();

	tc_set_data_role(USBC_PORT_C1, PD_ROLE_DFP);
	zassert_equal(PD_ROLE_DFP, pd_get_data_role(USBC_PORT_C1));

	/* Set active cable type */
	disc = pd_get_am_discovery_and_notify_access(USBC_PORT_C1,
						     TCPCI_MSG_SOP_PRIME);
	disc->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc->identity.product_t2.a2_rev30.active_elem = ACTIVE_RETIMER;
	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN2;
	prl_set_rev(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME, PD_REV30);

	/* Set cable VDO */
	disc->svid_cnt = 1;
	disc->svids[0].svid = USB_SID_DISPLAYPORT;
	disc->svids[0].discovery = PD_DISC_COMPLETE;
	disc->svids[0].mode_cnt = 1;
	disc->svdm_vers = SVDM_VER_2_1;
	cable_resp.dpam_ver = DPAM_VERSION_21;
	cable_resp.active_comp = DP21_ACTIVE_RETIMER_CABLE;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;

	/* Set device VDO */
	dev_disc = pd_get_am_discovery_and_notify_access(USBC_PORT_C1,
							 TCPCI_MSG_SOP);
	dev_disc->svid_cnt = 1;
	dev_disc->svids[0].svid = USB_SID_DISPLAYPORT;
	dev_disc->svids[0].discovery = PD_DISC_COMPLETE;
	dev_disc->svids[0].mode_cnt = 1;
	device_resp.dpam_ver = DPAM_VERSION_21;
	dev_disc->svids[0].mode_vdo[0] = device_resp.raw_value;

	/* Test DP mode with active cable */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION | BB_RETIMER_RE_TIMER_DRIVER |
		   BB_RETIMER_ACTIVE_PASSIVE |
		   BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(DP_HBR3) |
		   BB_RETIMER_DP_PIN_ASSIGNMENT;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	cable_resp.active_comp = DP21_OPTICAL_CABLE;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;

	/* Test DP mode with optical cable */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION | BB_RETIMER_TBT_CABLE_TYPE |
		   BB_RETIMER_ACTIVE_PASSIVE |
		   BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(DP_HBR3) |
		   BB_RETIMER_DP_PIN_ASSIGNMENT;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);
}
#endif

/** Test setting different options for DFP role */
ZTEST_USER(bb_retimer_no_tasks, test_bb_set_dfp_state)
{
	union tbt_mode_resp_device device_resp;
	union tbt_mode_resp_cable cable_resp;
	struct pd_discovery *disc, *dev_disc;
	uint32_t conn, exp_conn;
	const struct emul *emul = EMUL_DT_GET(BB_RETIMER_NODE);
	bool ack_required;

	set_test_runner_tid();

	tc_set_data_role(USBC_PORT_C1, PD_ROLE_DFP);
	zassert_equal(PD_ROLE_DFP, pd_get_data_role(USBC_PORT_C1));

	/* Test PD mux none mode with DFP should clear all bits in state */
	bb_emul_set_reg(emul, BB_RETIMER_REG_CONNECTION_STATE, 0x12144678);
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_NONE, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = 0;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Set active cable type */
	disc = pd_get_am_discovery_and_notify_access(USBC_PORT_C1,
						     TCPCI_MSG_SOP_PRIME);
	disc->identity.idh.product_type = IDH_PTYPE_ACABLE;
	disc->identity.product_t2.a2_rev30.active_elem = ACTIVE_RETIMER;
	disc->identity.product_t1.p_rev30.ss = USB_R30_SS_U32_U40_GEN2;
	prl_set_rev(USBC_PORT_C1, TCPCI_MSG_SOP_PRIME, PD_REV30);

	/* Set cable VDO */
	disc->svid_cnt = 1;
	disc->svids[0].svid = USB_VID_INTEL;
	disc->svids[0].discovery = PD_DISC_COMPLETE;
	disc->svids[0].mode_cnt = 1;
	cable_resp.tbt_alt_mode = TBT_ALTERNATE_MODE;
	cable_resp.tbt_cable_speed = TBT_SS_RES_0;
	cable_resp.tbt_rounded = TBT_GEN3_NON_ROUNDED;
	cable_resp.tbt_cable = TBT_CABLE_NON_OPTICAL;
	cable_resp.retimer_type = USB_NOT_RETIMER;
	cable_resp.lsrx_comm = BIDIR_LSRX_COMM;
	cable_resp.tbt_active_passive = TBT_CABLE_ACTIVE;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;

	/* Set device VDO */
	dev_disc = pd_get_am_discovery_and_notify_access(USBC_PORT_C1,
							 TCPCI_MSG_SOP);
	dev_disc->svid_cnt = 1;
	dev_disc->svids[0].svid = USB_VID_INTEL;
	dev_disc->svids[0].discovery = PD_DISC_COMPLETE;
	dev_disc->svids[0].mode_cnt = 1;
	device_resp.tbt_alt_mode = TBT_ALTERNATE_MODE;
	device_resp.tbt_adapter = TBT_ADAPTER_TBT3;
	device_resp.intel_spec_b0 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	device_resp.vendor_spec_b0 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	device_resp.vendor_spec_b1 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	dev_disc->svids[0].mode_vdo[0] = device_resp.raw_value;

	/* Test USB mode with active cable */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_USB_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_USB_3_CONNECTION | BB_RETIMER_USB_3_SPEED |
		   BB_RETIMER_RE_TIMER_DRIVER | BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test DP mode with active cable */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION | BB_RETIMER_RE_TIMER_DRIVER |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with active cable */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION | BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with retimer */
	cable_resp.retimer_type = USB_RETIMER;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION | BB_RETIMER_RE_TIMER_DRIVER |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with optical cable */
	cable_resp.retimer_type = USB_NOT_RETIMER;
	cable_resp.tbt_cable = TBT_CABLE_OPTICAL;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION | BB_RETIMER_TBT_CABLE_TYPE |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test DP mode with optical cable */
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_DP_ENABLED, &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_DP_CONNECTION | BB_RETIMER_TBT_CABLE_TYPE |
		   BB_RETIMER_ACTIVE_PASSIVE | BB_RETIMER_RE_TIMER_DRIVER;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with active link training */
	cable_resp.tbt_cable = TBT_CABLE_NON_OPTICAL;
	cable_resp.lsrx_comm = UNIDIR_LSRX_COMM;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn =
		BB_RETIMER_DATA_CONNECTION_PRESENT | BB_RETIMER_TBT_CONNECTION |
		BB_RETIMER_TBT_ACTIVE_LINK_TRAINING | BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with different cable speeds */
	cable_resp.lsrx_comm = BIDIR_LSRX_COMM;
	cable_resp.tbt_cable_speed = TBT_SS_U31_GEN1;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION |
		   BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(1) |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	cable_resp.tbt_cable_speed = TBT_SS_U32_GEN1_GEN2;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION |
		   BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(2) |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	cable_resp.tbt_cable_speed = TBT_SS_TBT_GEN3;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION |
		   BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(3) |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with TBT gen4 cable */
	cable_resp.tbt_cable_speed = TBT_SS_RES_0;
	cable_resp.tbt_rounded = TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn =
		BB_RETIMER_DATA_CONNECTION_PRESENT | BB_RETIMER_TBT_CONNECTION |
		BB_RETIMER_TBT_CABLE_GENERATION(1) | BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with legacy TBT adapter */
	cable_resp.tbt_rounded = TBT_GEN3_NON_ROUNDED;
	disc->svids[0].mode_vdo[0] = cable_resp.raw_value;
	device_resp.tbt_adapter = TBT_ADAPTER_TBT2_LEGACY;
	dev_disc->svids[0].mode_vdo[0] = device_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION | BB_RETIMER_TBT_TYPE |
		   BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with Intel specific b0 */
	device_resp.tbt_adapter = TBT_ADAPTER_TBT3;
	device_resp.intel_spec_b0 = VENDOR_SPECIFIC_SUPPORTED;
	dev_disc->svids[0].mode_vdo[0] = device_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn = BB_RETIMER_DATA_CONNECTION_PRESENT |
		   BB_RETIMER_TBT_CONNECTION | BB_RETIMER_ACTIVE_PASSIVE;

	if (IS_ENABLED(CONFIG_USBC_RETIMER_INTEL_BB_VPRO_CAPABLE))
		exp_conn |= BB_RETIMER_VPRO_DOCK_DP_OVERDRIVE;

	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);

	/* Test TBT mode with vendor specific b1 */
	device_resp.intel_spec_b0 = VENDOR_SPECIFIC_NOT_SUPPORTED;
	device_resp.vendor_spec_b1 = VENDOR_SPECIFIC_SUPPORTED;
	dev_disc->svids[0].mode_vdo[0] = device_resp.raw_value;
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.set(usb_muxes[USBC_PORT_C1].mux,
					 USB_PD_MUX_TBT_COMPAT_ENABLED,
					 &ack_required),
		      NULL);
	zassert_false(ack_required, "ACK is never required for BB retimer");
	conn = bb_emul_get_reg(emul, BB_RETIMER_REG_CONNECTION_STATE);
	exp_conn =
		BB_RETIMER_DATA_CONNECTION_PRESENT | BB_RETIMER_TBT_CONNECTION |
		BB_RETIMER_VPRO_DOCK_DP_OVERDRIVE | BB_RETIMER_ACTIVE_PASSIVE;
	zassert_equal(exp_conn, conn, "Expected state 0x%lx, got 0x%lx",
		      exp_conn, conn);
}

/** Test BB retimer init */
ZTEST_USER(bb_retimer, test_bb_init)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_USB_C1_LS_EN_PATH, gpios));
	const struct emul *emul = EMUL_DT_GET(BB_RETIMER_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bb_retimer_get_i2c_common_data(emul);

	zassert_not_null(gpio_dev, "Cannot get GPIO device");

	/* Set AP to normal state and wait for chipset task */
	test_set_chipset_to_s0();

	/* Setup emulator fail on read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  BB_RETIMER_REG_VENDOR_ID);
	/* Test fail on vendor ID read */
	zassert_equal(EC_ERROR_INVAL,
		      bb_usb_retimer.init(usb_muxes[USBC_PORT_C1].mux), NULL);
	/* Enable pins should be set always after init, when AP is on */
	zassert_equal(1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_LS_EN_PORT),
		      NULL);
	zassert_equal(
		1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_RT_RST_ODL_PORT),
		NULL);

	/* Setup wrong vendor ID */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	bb_emul_set_reg(emul, BB_RETIMER_REG_VENDOR_ID, 0x12144678);
	/* Test fail on wrong vendor ID */
	zassert_equal(EC_ERROR_INVAL,
		      bb_usb_retimer.init(usb_muxes[USBC_PORT_C1].mux), NULL);
	zassert_equal(1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_LS_EN_PORT),
		      NULL);
	zassert_equal(
		1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_RT_RST_ODL_PORT),
		NULL);

	/* Setup emulator fail on device ID read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  BB_RETIMER_REG_DEVICE_ID);
	bb_emul_set_reg(emul, BB_RETIMER_REG_VENDOR_ID, BB_RETIMER_VENDOR_ID_1);
	/* Test fail on device ID read */
	zassert_equal(EC_ERROR_INVAL,
		      bb_usb_retimer.init(usb_muxes[USBC_PORT_C1].mux), NULL);
	zassert_equal(1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_LS_EN_PORT),
		      NULL);
	zassert_equal(
		1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_RT_RST_ODL_PORT),
		NULL);

	/* Setup wrong device ID */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	bb_emul_set_reg(emul, BB_RETIMER_REG_DEVICE_ID, 0x12144678);
	/* Test fail on wrong device ID */
	zassert_equal(EC_ERROR_INVAL,
		      bb_usb_retimer.init(usb_muxes[USBC_PORT_C1].mux), NULL);
	zassert_equal(1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_LS_EN_PORT),
		      NULL);
	zassert_equal(
		1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_RT_RST_ODL_PORT),
		NULL);

	/* Test successful init */
	bb_emul_set_reg(emul, BB_RETIMER_REG_DEVICE_ID, BB_RETIMER_DEVICE_ID);
	zassert_equal(EC_SUCCESS,
		      bb_usb_retimer.init(usb_muxes[USBC_PORT_C1].mux), NULL);
	zassert_equal(1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_LS_EN_PORT),
		      NULL);
	zassert_equal(
		1, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_RT_RST_ODL_PORT),
		NULL);

	/* Set AP to off state and wait for chipset task */
	test_set_chipset_to_g3();

	/* With AP off, init should fail and pins should be unset */
	zassert_equal(EC_ERROR_NOT_POWERED,
		      bb_usb_retimer.init(usb_muxes[USBC_PORT_C1].mux), NULL);
	zassert_equal(0, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_LS_EN_PORT),
		      NULL);

	crec_msleep(1);
	zassert_equal(
		0, gpio_emul_output_get(gpio_dev, GPIO_USB_C1_RT_RST_ODL_PORT),
		NULL);
}

/** Test BB retimer console command */
ZTEST_USER(bb_retimer, test_bb_console_cmd)
{
	int rv;

	/* Validate well formed shell commands */
	rv = shell_execute_cmd(get_ec_shell(), "retimer 1 r 2");
	zassert_ok(rv, "rv=%d", rv);
	rv = shell_execute_cmd(get_ec_shell(), "retimer 1 w 2 0");
	zassert_ok(rv, "rv=%d", rv);

	/* Validate errors for malformed shell commands */
	rv = shell_execute_cmd(get_ec_shell(), "retimer x");
	zassert_equal(EC_ERROR_PARAM_COUNT, rv, "rv=%d", rv);
	rv = shell_execute_cmd(get_ec_shell(), "retimer x r 2");
	zassert_equal(EC_ERROR_PARAM1, rv, "rv=%d", rv);
	rv = shell_execute_cmd(get_ec_shell(), "retimer 0 r 2");
	zassert_equal(EC_ERROR_UNIMPLEMENTED, rv, "rv=%d", rv);
	rv = shell_execute_cmd(get_ec_shell(), "retimer 1 x 2");
	zassert_equal(EC_ERROR_PARAM2, rv, "rv=%d", rv);
	rv = shell_execute_cmd(get_ec_shell(), "retimer 1 r x");
	zassert_equal(EC_ERROR_PARAM3, rv, "rv=%d", rv);
	rv = shell_execute_cmd(get_ec_shell(), "retimer 1 w 2 x");
	zassert_equal(EC_ERROR_PARAM4, rv, "rv=%d", rv);
}

ZTEST_SUITE(bb_retimer_no_tasks, drivers_predicate_pre_main, NULL, NULL, NULL,
	    NULL);

ZTEST_SUITE(bb_retimer, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
