/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the Cortex-M0 core
 */

#include "cpu.h"

void cpu_init(void)
{
	/* Catch unaligned access */
	CPU_NVIC_CCR |= CPU_NVIC_CCR_UNALIGN_TRAP;

	/* Set supervisor call (SVC) to priority 0 */
	CPU_NVIC_SHCSR2 = 0;

	/* Set lowest priority for PendSV */
	CPU_NVIC_SHCSR3 = (0xff << 16);
}
