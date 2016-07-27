/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CTS_COMMON_H
#define __CTS_COMMON_H

#include "console.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

#ifdef CTS_DEBUG
/* These debug tags should not be changed */
#define CTS_DEBUG_START "\n[DEBUG]\n"
#define CTS_DEBUG_END "\n[DEBUG_END]\n"

#define CTS_DEBUG_PRINTF(format, args...) \
	do { \
		CPRINTF(CTS_DEBUG_START format CTS_DEBUG_END, ## args); \
		cflush(); \
	} while (0)
#else
#define CTS_DEBUG_PRINTF(format, args...)
#endif

#define READ_WAIT_TIME_MS		100
#define CTS_INTERRUPT_TRIGGER_DELAY_US	(250 * MSEC)

/* In a single test, only one board can return unknown, the other must
 * return a useful result (i.e. success, failure, etc)
 */

enum cts_rc {
	#include "cts.rc"
};

#endif
