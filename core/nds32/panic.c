/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "panic.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

void report_panic(uint32_t *regs, uint32_t itype)
{
	panic_printf("=== EXCEP: ITYPE=%x ===\n", itype);
	panic_printf("R0  %08x R1  %08x R2  %08x R3  %08x\n",
		     regs[0], regs[1], regs[2], regs[3]);
	panic_printf("R4  %08x R5  %08x R6  %08x R7  %08x\n",
		     regs[4], regs[5], regs[6], regs[7]);
	panic_printf("R8  %08x R9  %08x R10 %08x R15 %08x\n",
		     regs[8], regs[9], regs[10], regs[11]);
	panic_printf("FP  %08x GP  %08x LP  %08x SP  %08x\n",
		     regs[12], regs[13], regs[14], regs[15]);
	panic_printf("IPC %08x IPSW   %05x\n", regs[16], regs[17]);
	if ((regs[17] & PSW_INTL_MASK) == (2 << PSW_INTL_SHIFT)) {
		/* 2nd level exception */
		uint32_t oipc;

		asm volatile("mfsr %0, $OIPC" : "=r"(oipc));
		panic_printf("OIPC %08x\n", oipc);
	}

	panic_reboot();
}

void panic_data_print(const struct panic_data *pdata)
{
}
