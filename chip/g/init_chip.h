/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INIT_CHIP_H
#define __CROS_EC_INIT_CHIP_H

/**
 * This is the current state of the PMU persistent registers. There are two
 * types: long life and pwrdn scratch. Long life will persist through any
 * reset other than POR. PWRDN scratch only survives deep sleep.
 *
 * LONG_LIFE_SCRATCH 0 - 2
 *	SCRATCH0 - Rollback counter
 *	SCRATCH1 - Board properties
 *	SCRATCH2
 *
 * PWRDN_SCRATCH  0 - 15 - Locked
 *
 * PWRDN_SCRATCH 16 - 27 - Can be used by RW
 *	SCRATCH16 - Indicator that firmware is running for debug purposes
 *	SCRATCH17 - deep sleep count
 *	SCRATCH18 - Preserving USB_DCFG through deep sleep
 *	SCRATCH19 - Preserving USB data sequencing PID through deep sleep
 *
 * PWRDN_SCRATCH 28 - 31 - Reserved for boot rom
 */


enum permission_level {
	PERMISSION_LOW = 0x00,
	PERMISSION_MEDIUM = 0x33,    /* APPS run at medium */
	PERMISSION_HIGH = 0x3C,
	PERMISSION_HIGHEST = 0x55
};

int runlevel_is_high(void);
void init_runlevel(const enum permission_level desired_level);

void init_jittery_clock(int highsec);
void init_jittery_clock_locking_optional(int highsec,
					 int enable, int lock_required);
void init_sof_clock(void);

#endif	/* __CROS_EC_INIT_CHIP_H */
