/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Watchdog common code */

#include "timer.h"
#include "watchdog.h"


/* Low priority task to reload the watchdog */
void watchdog_task(void)
{
	while (1) {
		usleep(WATCHDOG_RELOAD_MS * 1000);
		watchdog_reload();
	}
}
