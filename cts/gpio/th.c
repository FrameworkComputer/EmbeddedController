/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "timer.h"
#include "watchdog.h"

void cts_task(void)
{
	while (1) {
		watchdog_reload();
		sleep(1);
	}
}
