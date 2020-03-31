/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C VPD and CTVPD module.
 */
#include "charge_manager.h"
#include "mock/tcpc_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"

#define PORT0 0

/* TODO(b/153071799): Move these pd_* and pe_* function into mock */
__overridable void pd_request_power_swap(int port)
{}

uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}

const uint32_t * const pd_get_src_caps(int port)
{
	return NULL;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
}

__overridable void pe_invalidate_explicit_contract(int port)
{
}
/* End pd_ mock section */

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

void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil)
{
	/* Do Nothing, but needed for linking */
}

__maybe_unused static int test_mux_con_dis_as_src(void)
{
	mock_tcpc.should_print_call = false;
	mock_usb_mux.num_set_calls = 0;

	/* Update CC lines send state machine event to process */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RD;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);
	pd_set_dual_role(0, PD_DRP_TOGGLE_ON);

	/* This wait trainsitions through AttachWait.SRC then Attached.SRC */
	task_wait_event(SECOND);

	/* We are in Attached.SRC now */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_USB_ENABLED, "%d");
	TEST_EQ(mock_usb_mux.num_set_calls, 1, "%d");

	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* This wait will go through TryWait.SNK then to Unattached.SNK */
	task_wait_event(10 * SECOND);

	/* We are in Unattached.SNK. The mux should have detached */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_NONE, "%d");
	TEST_EQ(mock_usb_mux.num_set_calls, 2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_mux_con_dis_as_snk(void)
{
	mock_tcpc.should_print_call = false;
	mock_usb_mux.num_set_calls = 0;

	/* Update CC lines send state machine event to process */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_RP_3_0;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 1;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* This wait will go through AttachWait.SNK to Attached.SNK */
	task_wait_event(5 * SECOND);

	/*
	 * We are in Attached.SNK now, but the port partner isn't data capable
	 * so we should not connect the USB data mux.
	 */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_NONE, "%d");

	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.vbus_level = 0;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);

	/* This wait will go through TryWait.SNK then to Unattached.SNK */
	task_wait_event(10 * SECOND);

	/* We are in Unattached.SNK. The mux should have detached */
	TEST_EQ(mock_usb_mux.state, USB_PD_MUX_NONE, "%d");
	TEST_LE(mock_usb_mux.num_set_calls, 2, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_power_role_set(void)
{
	mock_tcpc.num_calls_to_set_header = 0;

	/* Update CC lines send state machine event to process */
	mock_tcpc.cc1 = TYPEC_CC_VOLT_OPEN;
	mock_tcpc.cc2 = TYPEC_CC_VOLT_RD;
	task_set_event(TASK_ID_PD_C0, PD_EVENT_CC, 0);
	task_wait_event(10 * SECOND);

	/* We are in Attached.SRC now */
	TEST_EQ(mock_tcpc.last.power_role, PD_ROLE_SOURCE, "%d");
	TEST_EQ(mock_tcpc.last.data_role, PD_ROLE_DFP, "%d");

	/*
	 * We allow 2 separate calls to update the header since power and data
	 * role updates can be separate calls depending on the state is came
	 * from.
	 */
	TEST_LE(mock_tcpc.num_calls_to_set_header, 2, "%d");

	return EC_SUCCESS;
}

/* Reset the mocks before each test */
void before_test(void)
{
	mock_usb_mux_reset();
	mock_tcpc_reset();

	tc_restart_tcpc(PORT0);

	/* Ensure that PD task initializes its state machine and settles */
	task_wake(TASK_ID_PD_C0);
	task_wait_event(SECOND);

	/* Print out TCPC calls for easier debugging */
	mock_tcpc.should_print_call = true;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_mux_con_dis_as_src);
	RUN_TEST(test_mux_con_dis_as_snk);
	RUN_TEST(test_power_role_set);

	/* Do basic state machine sanity checks last. */
	RUN_TEST(test_tc_no_parent_cycles);
	RUN_TEST(test_tc_no_empty_state);
	RUN_TEST(test_tc_all_states_named);

	test_print_result();
}
