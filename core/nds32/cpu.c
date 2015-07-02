/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the N8 core
 */

#include "cpu.h"
#include "registers.h"

void cpu_init(void)
{
	/* DLM initialization is done in init.S */

	uint32_t image_type = (uint32_t)cpu_init;

	/* To change interrupt vector base if at RW image */
	if (image_type > CONFIG_RW_MEM_OFF)
		/* Interrupt Vector Table Base Address, in 64k Byte unit */
		IT83XX_GCTRL_IVTBAR = (CONFIG_RW_MEM_OFF >> 16) & 0xFF;
}
