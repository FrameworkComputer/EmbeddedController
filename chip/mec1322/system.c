/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : MEC1322 hardware specific implementation */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"


#define DUMMY(x) x {}
#define DUMMY_int(x) x { return 0; }

DUMMY(void system_hibernate(uint32_t seconds, uint32_t microseconds));
DUMMY_int(int system_get_vbnvcontext(uint8_t *block));
DUMMY_int(int system_set_vbnvcontext(const uint8_t *block));

void system_pre_init(void)
{
	/* Enable direct NVIC */
	MEC1322_EC_INT_CTRL |= 1;

	/* Deassert nSIO_RESET */
	MEC1322_PCR_PWR_RST_CTL &= ~(1 << 0);
}

void system_reset(int flags)
{
	/* Trigger watchdog in 1ms */
	MEC1322_WDG_LOAD = 1;
	MEC1322_WDG_CTL |= 1;
}

const char *system_get_chip_vendor(void)
{
	return "smsc";
}

const char *system_get_chip_name(void)
{
	switch (MEC1322_CHIP_DEV_ID) {
	case 0x15:
		return "mec1322";
	default:
		return "unknown";
	}
}

static char to_hex(int x)
{
	if (x >= 0 && x <= 9)
		return '0' + x;
	return 'a' + x - 10;
}

const char *system_get_chip_revision(void)
{
	static char buf[3];
	uint8_t rev = MEC1322_CHIP_DEV_REV;

	buf[0] = to_hex(rev / 16);
	buf[1] = to_hex(rev & 0xf);
	buf[2] = '\0';
	return buf;
}
