/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "registers.h"
#include "task.h"

static void fpu_init(void)
{
	task_enable_irq(STM32_IRQ_FPU);
}
DECLARE_HOOK(HOOK_INIT, fpu_init, HOOK_PRIO_DEFAULT);

__attribute__((naked)) void IRQ_HANDLER(STM32_IRQ_FPU)(void)
{
	/* Naked call so we can extract raw LR and SP */
	asm volatile("mov r0, lr\n"
		     "mov r1, sp\n"
		     /*
		      * By default Floating-point context control register
		      * (FPCCR) have ASPEN and LSPEN bits enabled (see reset
		      * value in PM0214, 4.6.2 Floating-point context control
		      * register (FPCCR)). This means that lazy floating-point
		      * context save and restore is enabled. To save context on
		      * stack it's necessary to perform read access from FPU
		      * (see PM0214 4.6.7 Enabling and clearing FPU exception
		      * interrupts).
		      */
		     "vmrs r2, fpscr\n"
		     /*
		      * Must push registers in pairs to keep 64-bit aligned
		      * stack for ARM EABI.
		      */
		     "push {r0, lr}\n"
		     "bl fpu_irq\n"
		     "pop {r0, pc}\n");
}
const struct irq_priority __keep IRQ_PRIORITY(STM32_IRQ_FPU)
	__attribute__((section(".rodata.irqprio")))
		= {STM32_IRQ_FPU, 0}; /* highest priority */
