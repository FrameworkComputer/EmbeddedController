/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "uart.h"
#include "timer.h"
#include "watchdog.h"
#include "dut_common.h"
#include "cts_common.h"

enum cts_rc debug_test(void)
{
	CTS_DEBUG_PRINTF("You should see #'s 1-4 on sequential lines:");
	CTS_DEBUG_PRINTF("1");
	CTS_DEBUG_PRINTF("2\n3");
	CTS_DEBUG_PRINTF("4");
	return CTS_RC_SUCCESS;
}

enum cts_rc success_test(void)
{
	CTS_DEBUG_PRINTF("Expect: Success");
	return CTS_RC_SUCCESS;
}

enum cts_rc fail_dut_test(void)
{
	CTS_DEBUG_PRINTF("Expect: Failure");
	return CTS_RC_SUCCESS;
}

enum cts_rc fail_th_test(void)
{
	CTS_DEBUG_PRINTF("Expect: Failure");
	return CTS_RC_FAILURE;
}

enum cts_rc fail_both_test(void)
{
	CTS_DEBUG_PRINTF("Expect: Failure");
	return CTS_RC_FAILURE;
}

enum cts_rc bad_sync_and_success_test(void)
{
	CTS_DEBUG_PRINTF("Expect: Bad Sync");
	return CTS_RC_BAD_SYNC;
}

enum cts_rc bad_sync_both_test(void)
{
	CTS_DEBUG_PRINTF("Expect: Bad Sync");
	return CTS_RC_BAD_SYNC;
}

enum cts_rc hang_test(void)
{
	CTS_DEBUG_PRINTF("This and next, expect: Corrupted");
	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	enum cts_rc result;
	int i;

	cflush();
	for (i = 0; i < CTS_TEST_ID_COUNT; i++) {
		sync();
		CPRINTF("\n%s start\n", tests[i].name);
		result = tests[i].run();
		CPRINTF("\n%s end %d\n", tests[i].name, result);
		cflush();
	}

	CPRINTS("Meta test finished");
	cflush();
	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
