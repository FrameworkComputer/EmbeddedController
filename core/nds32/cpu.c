/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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
	/* Global interrupt enable */
	asm volatile ("setgie.e");
}
