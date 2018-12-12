/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * mt_scp Stepping Stone functions on CPU reset.
 *
 * SCP assumes vector table at CONFIG_RAM_BASE. However, on cortex-m resetting,
 * it would load 0x0 to SP(r13) and load 0x04 to PC(r15).  Stepping stones copy
 * these two very special values from CONFIG_RAM_BASE, CONFIG_RAM_BASE + 0x04
 * to 0x0, 0x4 resepctively.
 */

#include "common.h"
#include "link_defs.h"

extern void *stack_end;
extern void reset(void);

__SECTION_KEEP(stepping_stone) const void *ss_header[2] = {
	&stack_end,
	&reset
};
