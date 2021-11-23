/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "asm_define.h"
#include "config.h"
#include "panic.h"

void unused(void)
{
	ASM_DEFINE_OFFSET("ASM_PANIC_DATA_CM_REGS_OFFSET", struct panic_data,
			  cm.regs);
	ASM_DEFINE("ASM_PANIC_DATA_PTR", PANIC_DATA_PTR);
	ASM_DEFINE("ASM_PANIC_STACK_ADDR", ((uint32_t)PANIC_DATA_PTR) & ~7);
}
