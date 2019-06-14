/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "util.h"
#include "power_mgt.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)


void clock_init(void)
{
	/* No initialization for clock on ISH */
}

void clock_refresh_console_in_use(void)
{
	/**
	 * on ISH, uart interrupt can only wakeup ISH from low power state via
	 * CTS pin, but most ISH platforms only have Rx and Tx pins, no CTS pin
	 * exposed, so, we need block ISH enter low power state for a while
	 * when console is in use
	 */
	ish_pm_refresh_console_in_use();
}
