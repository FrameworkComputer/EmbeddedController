/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C VPD and CTVPD module.
 */
#include "task.h"
#include "timer.h"
#include "test_util.h"
#include "usb_sm_checks.h"
#include "charge_manager.h"

void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil)
{
	/* Do Nothing, but needed for linking */
}

void run_test(void)
{
	test_reset();

	/* Ensure that PD task initializes its state machine */
	task_wake(TASK_ID_PD_C0);
	task_wait_event(5 * MSEC);

	/* Do basic state machine sanity checks last. */
	RUN_TEST(test_tc_no_parent_cycles);
	RUN_TEST(test_tc_no_empty_state);
	RUN_TEST(test_tc_all_states_named);

	test_print_result();
}
