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
}

void run_test(int argc, char **argv)
{
	test_reset();

	/* Do basic state machine validity checks last. */
	RUN_TEST(test_pe_no_parent_cycles);

	test_print_result();
}
