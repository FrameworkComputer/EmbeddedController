/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tc_sm.h"
#include "usb_tcpmv2_compliance.h"

void before_test(void)
{
	partner_set_pd_rev(PD_REV30);
	partner_tx_msg_id_reset(TCPCI_MSG_SOP_ALL);

	mock_usb_mux_reset();
	mock_tcpci_reset();

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE);
	task_wait_event(SECOND);

	/*
	 * Default to not allowing DUT to TRY.SRC and set it to be allowed
	 * specifically in the TRY.SRC tests
	 */
	tc_try_src_override(TRY_SRC_OVERRIDE_OFF);
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_td_pd_ll_e3_dfp);
	RUN_TEST(test_td_pd_ll_e3_ufp);
	RUN_TEST(test_td_pd_ll_e4_dfp);
	RUN_TEST(test_td_pd_ll_e4_ufp);
	RUN_TEST(test_td_pd_ll_e5_dfp);
	RUN_TEST(test_td_pd_ll_e5_ufp);
	RUN_TEST(test_td_pd_src_e1);
	RUN_TEST(test_td_pd_src_e2);
	RUN_TEST(test_td_pd_src_e5);

	RUN_TEST(test_td_pd_src3_e1);
	RUN_TEST(test_td_pd_src3_e7);
	RUN_TEST(test_td_pd_src3_e8);
	RUN_TEST(test_td_pd_src3_e9);
	RUN_TEST(test_td_pd_src3_e26);
	RUN_TEST(test_td_pd_src3_e32);
	RUN_TEST(test_td_pd_snk3_e12);

	RUN_TEST(test_td_pd_vndi3_e3_dfp);
	RUN_TEST(test_td_pd_vndi3_e3_ufp);

	RUN_TEST(test_connect_as_nonpd_sink);
	RUN_TEST(test_retry_count_sop);
	RUN_TEST(test_retry_count_hard_reset);

	test_print_result();
}
