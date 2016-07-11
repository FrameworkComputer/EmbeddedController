/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "watchdog.h"
#include "uart.h"
#include "timer.h"
#include "watchdog.h"
#include "dut_common.h"
#include "cts_common.h"

void cts_task(void)
{
	sync();
	CPRINTS("Successful Sync!");
	uart_flush_output();
	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
