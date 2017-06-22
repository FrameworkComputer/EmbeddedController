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
#define CPRINTL(format, args...) CPRINTS("%s:%d: "format, \
					 __func__, __LINE__, ## args)

#define READ_WAIT_TIME_MS		100
#define CTS_INTERRUPT_TRIGGER_DELAY_US	(250 * MSEC)

enum cts_rc {
	#include "cts.rc"
};

struct cts_test {
	enum cts_rc (*run)(void);
	char *name;
};

extern const int cts_test_count;

/**
 * Main loop where each test in a suite is executed
 *
 * A test suite can implement its custom loop as needed.
 *
 * Args:
 * @test: List of tests to run
 * @name: Name of the test to be printed on EC console
 */
void cts_main_loop(const struct cts_test* tests, const char *name);

/**
 * Callback function called at the beginning of the main loop
 */
void clean_state(void);

/**
 * Synchronize DUT and TH
 *
 * @return CTS_RC_SUCCESS if sync is successful
 */
enum cts_rc sync(void);

#endif
