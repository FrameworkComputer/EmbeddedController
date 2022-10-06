/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PE module.
 */
#include "battery.h"
#include "common.h"
#include "gpio.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_emsg.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pe.h"
#include "usb_pe_private.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"
#include "mock/usb_prl_mock.h"

#define pe_set_flag(_p, name) pe_set_fn((_p), (name##_FN))
#define pe_clr_flag(_p, name) pe_clr_fn((_p), (name##_FN))
#define pe_chk_flag(_p, name) pe_chk_fn((_p), (name##_FN))

/**
 * STUB Section
 */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT];
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT];

static bool prl_is_busy_flag;

bool prl_is_busy(int port)
{
	return prl_is_busy_flag;
}

int board_vbus_source_enabled(int port)
{
	return 0;
}
void tc_request_power_swap(int port)
{
	/* Do nothing */
}

void pd_set_vbus_discharge(int port, int enable)
{
	gpio_set_level(GPIO_USB_C0_DISCHARGE, enable);
}

test_static uint8_t tc_enabled = 1;

uint8_t tc_get_pd_enabled(int port)
{
	return tc_enabled;
}

void pd_comm_enable(int port, int enable)
{
	tc_enabled = !!enable;
}

bool pd_alt_mode_capable(int port)
{
	return 1;
}

void pd_set_suspend(int port, int suspend)
{
}

void pd_set_error_recovery(int port)
{
}

test_static void setup_source(void)
{
	/* Start PE. */
	task_wait_event(10 * MSEC);
	pe_set_flag(PORT0, PE_FLAGS_VDM_SETUP_DONE);
	pe_set_flag(PORT0, PE_FLAGS_EXPLICIT_CONTRACT);
	/* As long as we're hacking our way to ready, clear any DPM requests */
	pe_clr_dpm_requests(PORT0);
	set_state_pe(PORT0, PE_SRC_READY);
	task_wait_event(10 * MSEC);
	/* At this point, the PE should be running in PE_SRC_Ready. */
}

test_static void setup_sink(void)
{
	tc_set_power_role(PORT0, PD_ROLE_SINK);
	pd_comm_enable(PORT0, 0);
	task_wait_event(10 * MSEC);
	pd_comm_enable(PORT0, 1);
	task_wait_event(10 * MSEC);
	pe_set_flag(PORT0, PE_FLAGS_VDM_SETUP_DONE);
	pe_set_flag(PORT0, PE_FLAGS_EXPLICIT_CONTRACT);
	/* As long as we're hacking our way to ready, clear any DPM requests */
	pe_clr_dpm_requests(PORT0);
	set_state_pe(PORT0, PE_SNK_READY);
	task_wait_event(10 * MSEC);
	/* At this point, the PE should be running in PE_SNK_Ready. */
}
/**
 * Test section
 */
/* PE Fast Role Swap */
static int test_pe_frs(void)
{
	/*
	 * TODO(b/173791979): This test should validate PE boundary API
	 * differences -- not internal state changes.
	 */

	task_wait_event(10 * MSEC);
	TEST_ASSERT(pe_is_running(PORT0));

	/*
	 * FRS will only trigger when we are SNK, with an Explicit
	 * contract.  So set this state up manually.  Also ensure any
	 * background tasks (ex. discovery) aren't running.
	 */
	tc_prs_src_snk_assert_rd(PORT0);
	pe_set_flag(PORT0, PE_FLAGS_VDM_SETUP_DONE);
	pe_set_flag(PORT0, PE_FLAGS_EXPLICIT_CONTRACT);
	pe_clr_dpm_requests(PORT0);
	set_state_pe(PORT0, PE_SNK_READY);
	task_wait_event(10 * MSEC);
	TEST_ASSERT(get_state_pe(PORT0) == PE_SNK_READY);

	/*
	 * Trigger the Fast Role Switch from simulated ISR
	 */
	pd_got_frs_signal(PORT0);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_SIGNALED));

	/*
	 * Verify we detected FRS and ready to start swap
	 */
	task_wait_event(10 * MSEC);
	TEST_ASSERT(get_state_pe(PORT0) == PE_PRS_SNK_SRC_SEND_SWAP);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));
	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_EXPLICIT_CONTRACT));

	/*
	 * Make sure that we sent FR_Swap
	 */
	task_wait_event(10 * MSEC);
	TEST_ASSERT(mock_prl_get_last_sent_ctrl_msg(PORT0) == PD_CTRL_FR_SWAP);
	TEST_ASSERT(get_state_pe(PORT0) == PE_PRS_SNK_SRC_SEND_SWAP);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));
	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);

	/*
	 * Accept the partners PS_RDY control message
	 */
	rx_emsg[PORT0].header = PD_HEADER(PD_CTRL_ACCEPT, 0, 0, 0, 0, 0, 0);
	pe_set_flag(PORT0, PE_FLAGS_MSG_RECEIVED);
	task_wait_event(10 * MSEC);
	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_MSG_RECEIVED));
	TEST_ASSERT(get_state_pe(PORT0) == PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));

	/*
	 * Send back our PS_RDY
	 */
	rx_emsg[PORT0].header = PD_HEADER(PD_CTRL_PS_RDY, 0, 0, 0, 0, 0, 0);
	pe_set_flag(PORT0, PE_FLAGS_MSG_RECEIVED);
	TEST_ASSERT(!tc_is_attached_src(PORT0));
	task_wait_event(10 * MSEC);
	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_MSG_RECEIVED));
	TEST_ASSERT(tc_is_attached_src(PORT0));
	TEST_ASSERT(get_state_pe(PORT0) == PE_PRS_SNK_SRC_SOURCE_ON);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));

	/*
	 * After delay we are ready to send our PS_RDY
	 */
	task_wait_event(PD_POWER_SUPPLY_TURN_ON_DELAY);
	TEST_ASSERT(get_state_pe(PORT0) == PE_PRS_SNK_SRC_SOURCE_ON);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));
	TEST_ASSERT(mock_prl_get_last_sent_ctrl_msg(PORT0) == PD_CTRL_PS_RDY);

	/*
	 * Fake the Transmit complete and this will bring us to Source Startup
	 */
	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);
	task_wait_event(10 * MSEC);
	TEST_ASSERT(get_state_pe(PORT0) == PE_SRC_STARTUP);
	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));

	return EC_SUCCESS;
}

static int test_snk_give_source_cap(void)
{
	setup_sink();

	/*
	 * Receive a Get_Source_Cap message; respond with Source_Capabilities
	 * and return to PE_SNK_Ready once sent.
	 */
	rx_emsg[PORT0].header =
		PD_HEADER(PD_CTRL_GET_SOURCE_CAP, 0, 0, 0, 0, 0, 0);
	pe_set_flag(PORT0, PE_FLAGS_MSG_RECEIVED);
	task_wait_event(10 * MSEC);

	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_MSG_RECEIVED));
	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_TX_COMPLETE));
	TEST_EQ(mock_prl_get_last_sent_data_msg(PORT0), PD_DATA_SOURCE_CAP,
		"%d");
	TEST_EQ(get_state_pe(PORT0), PE_DR_SNK_GIVE_SOURCE_CAP, "%d");

	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);
	task_wait_event(10 * MSEC);
	TEST_EQ(get_state_pe(PORT0), PE_SNK_READY, "%d");

	return EC_SUCCESS;
}

static int test_vbus_gpio_discharge(void)
{
	pd_set_vbus_discharge(PORT0, 1);
	TEST_EQ(gpio_get_level(GPIO_USB_C0_DISCHARGE), 1, "%d");

	pd_set_vbus_discharge(PORT0, 0);
	TEST_EQ(gpio_get_level(GPIO_USB_C0_DISCHARGE), 0, "%d");

	return EC_SUCCESS;
}

test_static int test_extended_message_not_supported(void)
{
	memset(rx_emsg[PORT0].buf, 0, ARRAY_SIZE(rx_emsg[PORT0].buf));

	/*
	 * Receive an extended, non-chunked message; expect a Not Supported
	 * response.
	 */
	rx_emsg[PORT0].header = PD_HEADER(PD_DATA_BATTERY_STATUS, PD_ROLE_SINK,
					  PD_ROLE_UFP, 0, PDO_MAX_OBJECTS,
					  PD_REV30, 1);
	*(uint16_t *)rx_emsg[PORT0].buf =
		PD_EXT_HEADER(0, 0, ARRAY_SIZE(rx_emsg[PORT0].buf)) & ~BIT(15);
	pe_set_flag(PORT0, PE_FLAGS_MSG_RECEIVED);
	mock_prl_clear_last_sent_msg(PORT0);
	task_wait_event(10 * MSEC);

	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);
	task_wait_event(10 * MSEC);
	TEST_EQ(mock_prl_get_last_sent_ctrl_msg(PORT0), PD_CTRL_NOT_SUPPORTED,
		"%d");
	/* At this point, the PE should again be running in PE_SRC_Ready. */

	/*
	 * Receive an extended, chunked, single-chunk message; expect a Not
	 * Supported response.
	 */
	rx_emsg[PORT0].header = PD_HEADER(PD_DATA_BATTERY_STATUS, PD_ROLE_SINK,
					  PD_ROLE_UFP, 0, PDO_MAX_OBJECTS,
					  PD_REV30, 1);
	*(uint16_t *)rx_emsg[PORT0].buf =
		PD_EXT_HEADER(0, 0, PD_MAX_EXTENDED_MSG_CHUNK_LEN);
	pe_set_flag(PORT0, PE_FLAGS_MSG_RECEIVED);
	mock_prl_clear_last_sent_msg(PORT0);
	task_wait_event(10 * MSEC);

	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);
	task_wait_event(10 * MSEC);
	TEST_EQ(mock_prl_get_last_sent_ctrl_msg(PORT0), PD_CTRL_NOT_SUPPORTED,
		"%d");
	/* At this point, the PE should again be running in PE_SRC_Ready. */

	/*
	 * Receive an extended, chunked, multi-chunk message; expect a Not
	 * Supported response after tChunkingNotSupported (not earlier).
	 */
	rx_emsg[PORT0].header = PD_HEADER(PD_DATA_BATTERY_STATUS, PD_ROLE_SINK,
					  PD_ROLE_UFP, 0, PDO_MAX_OBJECTS,
					  PD_REV30, 1);
	*(uint16_t *)rx_emsg[PORT0].buf =
		PD_EXT_HEADER(0, 0, ARRAY_SIZE(rx_emsg[PORT0].buf));
	pe_set_flag(PORT0, PE_FLAGS_MSG_RECEIVED);
	mock_prl_clear_last_sent_msg(PORT0);
	task_wait_event(10 * MSEC);
	/*
	 * The PE should stay in PE_SRC_Chunk_Received for
	 * tChunkingNotSupported.
	 */
	task_wait_event(10 * MSEC);
	TEST_NE(mock_prl_get_last_sent_ctrl_msg(PORT0), PD_CTRL_NOT_SUPPORTED,
		"%d");

	task_wait_event(PD_T_CHUNKING_NOT_SUPPORTED);
	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);
	task_wait_event(10 * MSEC);
	TEST_EQ(mock_prl_get_last_sent_ctrl_msg(PORT0), PD_CTRL_NOT_SUPPORTED,
		"%d");
	/* At this point, the PE should again be running in PE_SRC_Ready. */

	/*
	 * TODO(b/160374787): Test responding with Not Supported to control
	 * messages requesting extended messages as responses.
	 */

	return EC_SUCCESS;
}

test_static int test_extended_message_not_supported_src(void)
{
	setup_source();
	return test_extended_message_not_supported();
}

test_static int test_extended_message_not_supported_snk(void)
{
	setup_sink();
	return test_extended_message_not_supported();
}

test_static int test_prl_is_busy(enum pd_power_role pr)
{
	int ready_state;

	if (pr == PD_ROLE_SOURCE)
		ready_state = PE_SRC_READY;
	else
		ready_state = PE_SNK_READY;

	/* Start in ready state with Protocol Layer busy */
	TEST_ASSERT(get_state_pe(PORT0) == ready_state);
	prl_is_busy_flag = true;

	/* Make a request to perform a Port Discovery */
	pd_dpm_request(PORT0, DPM_REQUEST_PORT_DISCOVERY);
	task_wait_event(10 * MSEC);
	task_wait_event(10 * MSEC);

	/*
	 * We should still be in ready state because the Protocol
	 * Layer is busy and can't send our message at this time.
	 */
	TEST_ASSERT(get_state_pe(PORT0) == ready_state);

	/* Protocol Layer is not busy now */
	prl_is_busy_flag = false;
	task_wait_event(10 * MSEC);
	task_wait_event(10 * MSEC);

	/*
	 * The Protocol Layer is no longer busy so we can switch to the
	 * state that will handle sending the Port Discovery messages.
	 */
	TEST_ASSERT(get_state_pe(PORT0) != ready_state);

	return EC_SUCCESS;
}

test_static int test_prl_is_busy_snk(void)
{
	setup_sink();
	return test_prl_is_busy(PD_ROLE_SINK);
}

test_static int test_prl_is_busy_src(void)
{
	setup_source();
	return test_prl_is_busy(PD_ROLE_SOURCE);
}

static int test_send_caps_error(void)
{
	/*
	 * See section 8.3.3.4.1.1 PE_SRC_Send_Soft_Reset State and section
	 * 8.3.3.2.3 PE_SRC_Send_Capabilities State.
	 *
	 * Transition to the PE_SRC_Discovery state when:
	 *  1) The Protocol Layer indicates that the Message has not been sent
	 *     and we are presently not Connected
	 */
	mock_prl_clear_last_sent_msg(PORT0);
	pe_set_flag(PORT0, PE_FLAGS_PROTOCOL_ERROR);
	pe_clr_flag(PORT0, PE_FLAGS_PD_CONNECTION);
	set_state_pe(PORT0, PE_SRC_SEND_CAPABILITIES);
	task_wait_event(10 * MSEC);
	TEST_EQ(mock_prl_get_last_sent_ctrl_msg(PORT0), 0, "%d");
	TEST_EQ(get_state_pe(PORT0), PE_SRC_DISCOVERY, "%d");

	/*
	 * Send soft reset when:
	 *  1) The Protocol Layer indicates that the Message has not been sent
	 *     and we are already Connected
	 */
	mock_prl_clear_last_sent_msg(PORT0);
	pe_set_flag(PORT0, PE_FLAGS_PROTOCOL_ERROR);
	pe_set_flag(PORT0, PE_FLAGS_PD_CONNECTION);
	set_state_pe(PORT0, PE_SRC_SEND_CAPABILITIES);
	task_wait_event(10 * MSEC);
	TEST_EQ(mock_prl_get_last_sent_ctrl_msg(PORT0), PD_CTRL_SOFT_RESET,
		"%d");
	TEST_EQ(get_state_pe(PORT0), PE_SEND_SOFT_RESET, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_pe_frs);
	RUN_TEST(test_snk_give_source_cap);
	RUN_TEST(test_vbus_gpio_discharge);
#ifndef CONFIG_USB_PD_EXTENDED_MESSAGES
	RUN_TEST(test_extended_message_not_supported_src);
	RUN_TEST(test_extended_message_not_supported_snk);
#else
	RUN_TEST(test_prl_is_busy_src);
	RUN_TEST(test_prl_is_busy_snk);
#endif
	RUN_TEST(test_send_caps_error);
	/* Do basic state machine validity checks last. */
	RUN_TEST(test_pe_no_parent_cycles);

	test_print_result();
}
