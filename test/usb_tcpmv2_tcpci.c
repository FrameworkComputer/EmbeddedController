/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "mock/tcpci_i2c_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_tc_sm.h"

#define PORT0 0

enum mock_cc_state {
	MOCK_CC_SRC_OPEN = 0,
	MOCK_CC_SNK_OPEN = 0,
	MOCK_CC_SRC_RA = 1,
	MOCK_CC_SNK_RP_DEF = 1,
	MOCK_CC_SRC_RD = 2,
	MOCK_CC_SNK_RP_1_5 = 2,
	MOCK_CC_SNK_RP_3_0 = 3,
};
enum mock_connect_result {
	MOCK_CC_WE_ARE_SRC = 0,
	MOCK_CC_WE_ARE_SNK = 1,
};

__maybe_unused static void mock_set_cc(enum mock_connect_result cr,
	enum mock_cc_state cc1, enum mock_cc_state cc2)
{
	mock_tcpci_set_reg(TCPC_REG_CC_STATUS,
		TCPC_REG_CC_STATUS_SET(cr, cc1, cc2));
}

__maybe_unused static void mock_set_role(int drp, enum tcpc_rp_value rp,
	enum tcpc_cc_pull cc1, enum tcpc_cc_pull cc2)
{
	mock_tcpci_set_reg(TCPC_REG_ROLE_CTRL,
		TCPC_REG_ROLE_CTRL_SET(drp, rp, cc1, cc2));
}

static int mock_alert_count;

__maybe_unused static void mock_set_alert(int alert)
{
	mock_tcpci_set_reg(TCPC_REG_ALERT, alert);
	mock_alert_count = 1;
	schedule_deferred_pd_interrupt(PORT0);
}

uint16_t tcpc_get_alert_status(void)
{
	ccprints("mock_alert_count %d", mock_alert_count);
	if (mock_alert_count > 0) {
		mock_alert_count--;
		return PD_STATUS_TCPC_ALERT_0;
	}
	return 0;
}

const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int pd_check_vconn_swap(int port)
{
	return 1;
}

void board_reset_pd_mcu(void) {}

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_HOST_TCPC,
			.addr_flags = MOCK_TCPCI_I2C_ADDR_FLAGS,
		},
		.drv = &tcpci_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};

__maybe_unused static int test_connect_as_sink(void)
{
	task_wait_event(10 * SECOND);

	/* Simulate a non-PD power supply being plugged in. */
	mock_set_cc(MOCK_CC_WE_ARE_SNK, MOCK_CC_SNK_OPEN, MOCK_CC_SNK_RP_3_0);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	task_wait_event(50 * MSEC);

	mock_tcpci_set_reg(TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_PRES);
	mock_set_alert(TCPC_REG_ALERT_POWER_STATUS);

	task_wait_event(10 * SECOND);
	TEST_EQ(tc_is_attached_snk(PORT0), true, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_startup_and_resume(void)
{
	/* Should be in low power mode before AP boots. */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_COMMAND),
		TCPC_REG_COMMAND_I2CIDLE, "%d");
	task_wait_event(10 * SECOND);

	hook_notify(HOOK_CHIPSET_STARTUP);
	task_wait_event(5 * MSEC);
	hook_notify(HOOK_CHIPSET_RESUME);

	task_wait_event(10 * SECOND);
	/* Should be in low power mode and DRP auto-toggling with AP in S0. */
	TEST_EQ((mock_tcpci_get_reg(TCPC_REG_ROLE_CTRL)
		 & TCPC_REG_ROLE_CTRL_DRP_MASK),
		TCPC_REG_ROLE_CTRL_DRP_MASK, "%d");
	/* TODO: check previous command was TCPC_REG_COMMAND_LOOK4CONNECTION */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_COMMAND),
		TCPC_REG_COMMAND_I2CIDLE, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	mock_usb_mux_reset();
	mock_tcpci_reset();

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE, 0);
	task_wait_event(SECOND);
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_connect_as_sink);
	RUN_TEST(test_startup_and_resume);

	test_print_result();
}
