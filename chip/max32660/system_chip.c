/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 System module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "registers.h"
#include "gcr_regs.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

void chip_pre_init(void)
{
}

void system_pre_init(void)
{
}

void system_reset(int flags)
{
	MXC_GCR->rstr0 = MXC_F_GCR_RSTR0_SYSTEM;
	while (1)
		;
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* chip specific standby mode */
	CPRINTS("TODO: implement %s()", __func__);
}

const char *system_get_chip_vendor(void)
{
	return "maxim";
}

const char *system_get_chip_name(void)
{
	return "max32660";
}

const char *system_get_chip_revision(void)
{
	return "A1";
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_UNIMPLEMENTED;
}
