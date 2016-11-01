/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INIT_CHIP_H
#define __CROS_EC_INIT_CHIP_H

enum permission_level {
	PERMISSION_LOW = 0x00,
	PERMISSION_MEDIUM = 0x33,    /* APPS run at medium */
	PERMISSION_HIGH = 0x3C,
	PERMISSION_HIGHEST = 0x55
};

int runlevel_is_high(void);
void init_runlevel(const enum permission_level desired_level);

void init_jittery_clock(int highsec);
void init_sof_clock(void);

#endif	/* __CROS_EC_INIT_CHIP_H */
