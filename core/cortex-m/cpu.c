/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the Cortex-M core
 */

#include "cpu.h"

void cpu_init(void)
{
	/* Catch divide by 0 and unaligned access */
	CPU_NVIC_CCR |= CPU_NVIC_CCR_DIV_0_TRAP | CPU_NVIC_CCR_UNALIGN_TRAP;

	/* Enable reporting of memory faults, bus faults and usage faults */
	CPU_NVIC_SHCSR |= CPU_NVIC_SHCSR_MEMFAULTENA |
		CPU_NVIC_SHCSR_BUSFAULTENA | CPU_NVIC_SHCSR_USGFAULTENA;
}
