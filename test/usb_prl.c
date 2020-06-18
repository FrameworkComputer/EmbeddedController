/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Protocol Layer module.
 */
#include "common.h"
#include "task.h"
#include "tcpm.h"
#include "test_util.h"
#include "timer.h"
#include "usb_emsg.h"
#include "usb_pd_test_util.h"
#include "usb_pd.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "mock/tcpc_mock.h"

#define PORT0 0

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

void before_test(void)
{
}

void run_test(int argc, char **argv)
{
	/* TODO add tests here */

	/* Do basic state machine sanity checks last. */
	RUN_TEST(test_prl_no_parent_cycles);
	RUN_TEST(test_prl_no_empty_state);
	RUN_TEST(test_prl_all_states_named);

	test_print_result();
}

