/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cts_common.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"

enum cts_rc success_test(void)
{
	return CTS_RC_SUCCESS;
}

enum cts_rc fail_dut_test(void)
{
	return CTS_RC_FAILURE;
}

enum cts_rc fail_th_test(void)
{
	return CTS_RC_SUCCESS;
}

enum cts_rc fail_both_test(void)
{
	return CTS_RC_FAILURE;
}

enum cts_rc bad_sync_test(void)
{
	return CTS_RC_SUCCESS;
}

enum cts_rc bad_sync_both_test(void)
{
	return CTS_RC_BAD_SYNC;
}

enum cts_rc hang_test(void)
{
	while (1) {
		watchdog_reload();
		crec_sleep(1);
	}

	return CTS_RC_SUCCESS;
}

enum cts_rc did_not_start_test(void)
{
	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "Meta");
	task_wait_event(-1);
}
