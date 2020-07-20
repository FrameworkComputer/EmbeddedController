/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PE module.
 */
#include "battery.h"
#include "common.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_emsg.h"
#include "usb_mux.h"
#include "usb_pe.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"

/**
 * STUB Section
 */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT];
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT];

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

uint8_t tc_get_pd_enabled(int port)
{
	return 1;
}

bool pd_alt_mode_capable(int port)
{
	return 1;
}

void pd_set_suspend(int port, int suspend)
{

}
/**
 * Test section
 */
/* PE Fast Role Swap */
static int test_pe_frs(void)
{
	/*
	 * TODO: This test should validate PE boundary API differences -- not
	 * internal state changes.
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
	TEST_ASSERT(fake_prl_get_last_sent_ctrl_msg(PORT0) == PD_CTRL_FR_SWAP);
	TEST_ASSERT(get_state_pe(PORT0) == PE_PRS_SNK_SRC_SEND_SWAP);
	TEST_ASSERT(pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));

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
	TEST_ASSERT(fake_prl_get_last_sent_ctrl_msg(PORT0) == PD_CTRL_PS_RDY);

	/*
	 * Fake the Transmit complete and this will bring us to Source Startup
	 */
	pe_set_flag(PORT0, PE_FLAGS_TX_COMPLETE);
	task_wait_event(10 * MSEC);
	TEST_ASSERT(get_state_pe(PORT0) == PE_SRC_STARTUP);
	TEST_ASSERT(!pe_chk_flag(PORT0, PE_FLAGS_FAST_ROLE_SWAP_PATH));

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

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_pe_frs);
	RUN_TEST(test_vbus_gpio_discharge);

	/* Do basic state machine sanity checks last. */
	RUN_TEST(test_pe_no_parent_cycles);
	RUN_TEST(test_pe_no_empty_state);

	test_print_result();
}
