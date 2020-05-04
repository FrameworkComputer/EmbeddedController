/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the RISC-V core
 */

#include "cpu.h"

void cpu_init(void)
{
	/* bit3: Global interrupt enable (M-mode) */
	asm volatile ("csrsi mstatus, 0x8");
}
