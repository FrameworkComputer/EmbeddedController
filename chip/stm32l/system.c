/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "cpu.h"
#include "system.h"


static void check_reset_cause(void)
{
	system_set_reset_cause(SYSTEM_RESET_UNKNOWN);
}


void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* we are going to hibernate ... */
	while (1)
		;
}


int system_pre_init(void)
{
	check_reset_cause();

	return EC_SUCCESS;
}


int system_init(void)
{
	return EC_SUCCESS;
}


int system_reset(int is_cold)
{
	/* TODO: (crosbug.com/p/7470) support cold boot; this is a
	   warm boot. */
	CPU_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return */
	/* TODO: (crosbug.com/p/7471) should disable task swaps while
	   waiting */
	while (1)
		;

	return EC_ERROR_UNKNOWN;
}


int system_set_scratchpad(uint32_t value)
{
	return EC_SUCCESS;
}


uint32_t system_get_scratchpad(void)
{
	return 0xdeadbeef;
}
