/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PE module.
 */
#include "common.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_emsg.h"
#include "usb_mux.h"
#include "usb_pe.h"
#include "usb_pe_sm.h"
#include "usb_sm_checks.h"
#include "mock/usb_tc_sm_mock.h"
#include "mock/tcpc_mock.h"
#include "mock/usb_mux_mock.h"
#include "mock/usb_pd_mock.h"
#include "mock/usb_pd_dpm_mock.h"
#include "mock/dp_alt_mode_mock.h"
#include "mock/usb_prl_mock.h"

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};

void before_test(void)
{
	mock_tc_port_reset();
	mock_tcpc_reset();
	mock_usb_mux_reset();
	mock_pd_reset();
	mock_dpm_reset();
	mock_dp_alt_mode_reset();
	mock_prl_reset();

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE, 0);
	task_wait_event(SECOND);
}

test_static int test_send_caps_error(void)
{
	/* Enable PE as source, expect SOURCE_CAP. */
	mock_pd_port[PORT0].power_role = PD_ROLE_SOURCE;
	mock_tc_port[PORT0].pd_enable = 1;
	task_wait_event(10 * MSEC);
	TEST_EQ(fake_prl_get_last_sent_data_msg_type(PORT0),
		PD_DATA_SOURCE_CAP, "%d");
	fake_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);

	/* REQUEST 5V, expect ACCEPT, PS_RDY. */
	rx_emsg[PORT0].header = PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK,
			PD_ROLE_UFP, 0,
			1, PD_REV30, 0);
	rx_emsg[PORT0].len = 4;
	*(uint32_t *)rx_emsg[PORT0].buf = RDO_FIXED(1, 500, 500, 0);
	fake_prl_message_received(PORT0);
	task_wait_event(10 * MSEC);
	TEST_EQ(fake_prl_get_last_sent_ctrl_msg(PORT0),
		PD_CTRL_ACCEPT, "%d");
	fake_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);
	TEST_EQ(fake_prl_get_last_sent_ctrl_msg(PORT0),
		PD_CTRL_PS_RDY, "%d");
	fake_prl_message_sent(PORT0);
	task_wait_event(30 * MSEC);

	/* Expect VENDOR_DEF, reply NOT_SUPPORTED. */
	TEST_EQ(fake_prl_get_last_sent_data_msg_type(PORT0),
		PD_DATA_VENDOR_DEF, "%d");
	fake_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);
	rx_emsg[PORT0].header = PD_HEADER(PD_CTRL_NOT_SUPPORTED, PD_ROLE_SINK,
			PD_ROLE_UFP, 2,
			0, PD_REV30, 0);
	rx_emsg[PORT0].len = 0;
	fake_prl_message_received(PORT0);
	task_wait_event(30 * MSEC);

	/* Expect GET_SOURCE_CAP, reply NOT_SUPPORTED. */
	TEST_EQ(fake_prl_get_last_sent_ctrl_msg(PORT0),
		PD_CTRL_GET_SOURCE_CAP, "%d");
	fake_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);
	rx_emsg[PORT0].header = PD_HEADER(PD_CTRL_NOT_SUPPORTED, PD_ROLE_SINK,
			PD_ROLE_UFP, 2,
			0, PD_REV30, 0);
	rx_emsg[PORT0].len = 0;
	fake_prl_message_received(PORT0);
	task_wait_event(200 * MSEC);

	/*
	 * Now connected. Send GET_SOURCE_CAP, to check how error sending
	 * SOURCE_CAP is handled.
	 */
	rx_emsg[PORT0].header = PD_HEADER(PD_CTRL_GET_SOURCE_CAP, PD_ROLE_SINK,
			PD_ROLE_UFP, 3,
			0, PD_REV30, 0);
	rx_emsg[PORT0].len = 0;
	fake_prl_message_received(PORT0);
	task_wait_event(10 * MSEC);
	TEST_EQ(fake_prl_get_last_sent_data_msg_type(PORT0),
		PD_DATA_SOURCE_CAP, "%d");

	/* Simulate error sending SOURCE_CAP. */
	fake_prl_report_error(PORT0, ERR_TCH_XMIT);
	task_wait_event(20 * MSEC);

	/*
	 * Expect SOFT_RESET.
	 * See section 8.3.3.4.1.1 PE_SRC_Send_Soft_Reset State and section
	 * 8.3.3.2.3 PE_SRC_Send_Capabilities State.
	 * "The PE_SRC_Send_Soft_Reset state Shall be entered from any state
	 * when ... A Message has not been sent after retries to the Sink"
	 */
	TEST_EQ(fake_prl_get_last_sent_ctrl_msg(PORT0),
		PD_CTRL_SOFT_RESET, "%d");
	fake_prl_message_sent(PORT0);
	task_wait_event(5 * SECOND);

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_send_caps_error);

	/* Do basic state machine validity checks last. */
	RUN_TEST(test_pe_no_parent_cycles);

	test_print_result();
}
