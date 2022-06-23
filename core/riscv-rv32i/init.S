/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * RISC-V CPU initialization
 */

#include "config.h"

.macro vector name
.set \name\()_handler, unhandled_ec_irq
.weak \name\()_handler
j __entry_\()\name
.pushsection .text.vectirq
.global __entry_\()\name
__entry_\()\name:
        /* C routine handler */
        j \name\()_handler
.popsection
.pushsection .rodata.vecthandlers
.long \name\()_handler
.popsection
.endm

.section .text.vecttable
.align 2
__startup:
        j __reset       /* reset */
__irq:
        j __irq_isr     /* interrupts / exceptions */

.align 2
__ec_intc:
        vector irq_0    /* INT GROUP  0 */
        vector irq_1    /* INT GROUP  1 */
        vector irq_2    /* INT GROUP  2 */
        vector irq_3    /* INT GROUP  3 */
        vector irq_4    /* INT GROUP  4 */
        vector irq_5    /* INT GROUP  5 */
        vector irq_6    /* INT GROUP  6 */
        vector irq_7    /* INT GROUP  7 */
        vector irq_8    /* INT GROUP  8 */
        vector irq_9    /* INT GROUP  9 */
        vector irq_10   /* INT GROUP 10 */
        vector irq_11   /* INT GROUP 11 */
        vector irq_12   /* INT GROUP 12 */
        vector irq_13   /* INT GROUP 13 */
        vector irq_14   /* INT GROUP 14 */
        vector irq_15   /* INT GROUP 15 */
        vector syscall  /* system call (emulate INT GROUP 16) */

#ifdef CHIP_FAMILY_IT8XXX2
/*
 * E-flash signature used to enable specific function after power-on reset.
 * (HW mechanism)
 * The content of 16-bytes must be the following and at offset 0x80 of binary.
 * ----------------------------------------------------------------------------
 * 1st 2nd 3rd 4th 5th 6th   7th    8th  9th 10th 11th 12th 13th 14th 15th 16th
 * ----------------------------------------------------------------------------
 * A5h A5h A5h A5h A5h A5h [host] [flag] 85h  12h  5Ah  5Ah  AAh  AAh  55h  55h
 * ----------------------------------------------------------------------------
 * [host]: A4h = enable eSPI, A5h = enable LPC
 * [flag]:
 * bit7: it must be 1b.
 * bit6: it must be 0b.
 * bit5: it must be 1b.
 * bit4: 1b = 32.768KHz is from the internal clock generator.
 * bit3: it must be 0b.
 * bit2: it must be 1b.
 * bit1: it must be 0b.
 * bit0: it must be 0b.
 */
.org 0x80
.balign 16
.global eflash_sig
eflash_sig:
.byte 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5
#ifdef CONFIG_HOST_INTERFACE_ESPI
.byte 0xA4 /* eSPI */
#else
.byte 0xA5 /* LPC */
#endif
.byte 0xB4 /* flag of signature */
.byte 0x85, 0x12, 0x5A, 0x5A, 0xAA, 0xAA, 0x55, 0x55
/* flags: internal oscillator + implicit location */
#endif /* CHIP_FAMILY_IT8XXX2 */


.align 2
.section .text.vectirq
__irq_isr:
        /* save t2 to scratch register */
        csrw mscratch, t2
        /* save registers (sp, ra, t0, and t1) state at exception entry */
        la t2, excep_entry_saved_regs
        sw sp, 0*4(t2)
        sw ra, 1*4(t2)
        sw t0, 2*4(t2)
        sw t1, 3*4(t2)
        /* store return address register */
        la t2, ira
        sw ra, 0(t2)
        /* restore t2 */
        csrr t2, mscratch
        /* save ra, a0-7, t0-t6 (high memory address to low) */
        sw t6, -16*4(sp)
        sw t5, -15*4(sp)
        sw t4, -14*4(sp)
        sw t3, -13*4(sp)
        sw t2, -12*4(sp)
        sw t1, -11*4(sp)
        sw t0, -10*4(sp)
        sw a7, -9*4(sp)
        sw a6, -8*4(sp)
        sw a5, -7*4(sp)
        sw a4, -6*4(sp)
        sw a3, -5*4(sp)
        sw a2, -4*4(sp)
        sw a1, -3*4(sp)
        /* Don't change index of ra and a0 (see task_pre_init()) */
        sw a0, -2*4(sp)
        sw ra, -1*4(sp)
#ifdef CONFIG_FPU
        /* save ft0-11, fa0-7, and fcsr. */
        csrr t0, fcsr
        sw t0,    -37*4(sp)
        fsw fa7,  -36*4(sp)
        fsw fa6,  -35*4(sp)
        fsw fa5,  -34*4(sp)
        fsw fa4,  -33*4(sp)
        fsw fa3,  -32*4(sp)
        fsw fa2,  -31*4(sp)
        fsw fa1,  -30*4(sp)
        fsw fa0,  -29*4(sp)
        fsw ft11, -28*4(sp)
        fsw ft10, -27*4(sp)
        fsw ft9,  -26*4(sp)
        fsw ft8,  -25*4(sp)
        fsw ft7,  -24*4(sp)
        fsw ft6,  -23*4(sp)
        fsw ft5,  -22*4(sp)
        fsw ft4,  -21*4(sp)
        fsw ft3,  -20*4(sp)
        fsw ft2,  -19*4(sp)
        fsw ft1,  -18*4(sp)
        fsw ft0,  -17*4(sp)
        /*
         * Note: we never execute on this stack frame, so it does not need to
         * be 16-byte aligned.
         */
        addi sp, sp, -37*4
#else
        /*
         * Note: we never execute on this stack frame, so it does not need to
         * be 16-byte aligned.
         */
        addi sp, sp, -16*4
#endif
        /* Save sp to scratch register */
        csrw mscratch, sp
        /* Load top of system stack address into t0 for comparison */
        la t0, stack_end
        /*
         * Switch to system stack (which is in lower memory than task stack)
         * if we are not already operating with the system stack
         */
        bltu sp, t0, __sp_16byte_aligned
        mv sp, t0
__sp_16byte_aligned:
	/* in_interrupt = 1 */
	li t0, 1
	la t1, in_interrupt
        sb t0, 0(t1)
        /*
         * This ensures sp is 16-byte aligned. This only applies to when there
         * is an interrupt before tasks start. Otherwise stack_end is already
         * 16-byte aligned.
         */
        andi sp, sp, -16
        /* read exception cause */
        csrr t0, mcause
        /* isolate exception cause */
        andi t1, t0, 0x1f
        /* mcause = 11: external interrupt or environment call from M-mode */
        addi t1, t1, -11
        beqz t1, __irq_handler
        /* branch if this is an exceptoin (the interrupt bit of mcause is 0) */
        bgez t0, excep_handler
        /* This interrupt is unhandled */
        j unhandled_interrupt
__irq_handler:
        /* save a0, a1, and a2 for syscall */
        addi sp, sp, -4*3
        sw a0, 0(sp)
        sw a1, 1*4(sp)
        sw a2, 2*4(sp)
        jal start_irq_handler
        /* a0 = -1 if it cannot find the corresponding interrupt source */
        bltz a0, unhandled_interrupt
        /* restore a0, a1, and a2 */
        lw a0, 0(sp)
        lw a1, 1*4(sp)
        lw a2, 2*4(sp)
        addi sp, sp, 4*3
        /* get EC interrupt group 0-15 or 16:ecall */
        la t0, ec_int_group
        /* get corresponding isr */
        lw t1, 0(t0)
        slli t1, t1, 2
        la t0, __ec_intc
        add t0, t0, t1
        /* handle irq */
        jalr t0
        /* check whether we need to change the scheduled task */
        la t0, need_resched
        lw t1, 0(t0)
        bnez t1, __switch_task
.global __irq_exit
__irq_exit:
        jal end_irq_handler
	/* in_interrupt = 0 */
	la t0, in_interrupt
	sb zero, 0(t0)
        /* restore sp from scratch register */
        csrr sp, mscratch
#ifdef CONFIG_FPU
        addi sp, sp, 37*4
        /* restore ft0-11, fa0-7, and fcsr. */
        lw t0,    -37*4(sp)
        csrw fcsr, t0
        flw fa7,  -36*4(sp)
        flw fa6,  -35*4(sp)
        flw fa5,  -34*4(sp)
        flw fa4,  -33*4(sp)
        flw fa3,  -32*4(sp)
        flw fa2,  -31*4(sp)
        flw fa1,  -30*4(sp)
        flw fa0,  -29*4(sp)
        flw ft11, -28*4(sp)
        flw ft10, -27*4(sp)
        flw ft9,  -26*4(sp)
        flw ft8,  -25*4(sp)
        flw ft7,  -24*4(sp)
        flw ft6,  -23*4(sp)
        flw ft5,  -22*4(sp)
        flw ft4,  -21*4(sp)
        flw ft3,  -20*4(sp)
        flw ft2,  -19*4(sp)
        flw ft1,  -18*4(sp)
        flw ft0,  -17*4(sp)
#else
        addi sp, sp, 16*4
#endif
        /* restore ra, a0-a7, t0-t6 */
        lw t6, -16*4(sp)
        lw t5, -15*4(sp)
        lw t4, -14*4(sp)
        lw t3, -13*4(sp)
        lw t2, -12*4(sp)
        lw t1, -11*4(sp)
        lw t0, -10*4(sp)
        lw a7, -9*4(sp)
        lw a6, -8*4(sp)
        lw a5, -7*4(sp)
        lw a4, -6*4(sp)
        lw a3, -5*4(sp)
        lw a2, -4*4(sp)
        lw a1, -3*4(sp)
        lw a0, -2*4(sp)
        lw ra, -1*4(sp)
        mret

.text
.global __reset
__reset:
        /* disable interrupts */
        csrw mie, zero
.option push
.option norelax
        /* GP register is used to access .data and .bss (address +/- 2048) */
        la gp, __global_pointer$
.option pop
        /* Set system stack pointer. */
        la sp, stack_end
#ifdef CONFIG_FPU
        li t0, 0x6000
        csrw mstatus, t0
        csrw fcsr, zero
#else
        csrw mstatus, zero
#endif
        /*
         * move content of return address(ra) into t5 and then store the content
         * into variable "ec_reset_lp" later after memory initialization.
         */
        mv t5, ra
        /* Clear the link register */
        li ra, 0
        /* Clear the thread pointer register */
        li tp, 0
        /* set machine trap-handler base address */
        la t0, __irq
        csrw mtvec, t0
        /* reset scratch register */
        csrw mscratch, zero
#if defined(CHIP_FAMILY_RV55)
        j 1f /* doesn't support mideleg and medeleg */
#endif
        /* The M-mode handles interrupt/exception */
        csrwi mideleg, 0
        csrwi medeleg, 0
#if defined(IT83XX_CHIP_FLASH_SIZE_1MB) && defined(CHIP_FAMILY_IT8XXX2)
        /* ILM size is 1M bytes */
        la t0, IT83XX_GCTRL_EIDSR
        lb t1, 0(t0)
        andi t1, t1, 0xf0
        ori  t1, t1, 0x8
        sb t1, 0(t0)
#endif
1:
        /* Clear BSS */
        la t0, __bss_start
        la t1, __bss_end
bss_loop:
        sw zero, 0(t0)
        addi t0, t0, 4
        bltu t0, t1, bss_loop
        /* Copy initialized data to data section */
        la t0, __data_start
        la t1, __data_end
        la t2, __data_lma_start
data_loop:
        lw t3, 0(t2)
        sw t3, 0(t0)
        addi t0, t0, 4
        addi t2, t2, 4
        bltu t0, t1, data_loop
        /* store the content of t5 (ra after reset) into "ec_reset_lp" */
        la t0, ec_reset_lp
        sw t5, 0(t0)
#ifdef CHIP_FAMILY_IT8XXX2
        /* clear BRAM if it is not valid */
        jal chip_bram_valid
#endif
        /* Jump to C routine */
        jal main
        /* That should not return.  If it does, loop forever. */
        j .

.global unhandled_ec_irq
.global unhandled_interrupt
unhandled_ec_irq:
        li tp, 0xBAD1
        j __unhandled_irq
unhandled_interrupt:
        li tp, 0xBAD0
__unhandled_irq:
        slli tp, tp, 16
        la t0, ec_int
        lw t0, 0(t0)
        add tp, tp, t0
        j excep_handler /* display exception with TP bad[0|1]<ec_int> */

.global excep_handler
excep_handler:
        /* save t2 */
        csrw mscratch, t2
        /* restore registers (sp, ra, t0, and t1) state */
        la t2, excep_entry_saved_regs
        lw sp, 0*4(t2)
        lw ra, 1*4(t2)
        lw t0, 2*4(t2)
        lw t1, 3*4(t2)
        /* restore t2 */
        csrr t2, mscratch
        /* save sp to scratch register */
        csrw mscratch, sp
        la sp, saved_regs
        /* save sp, ra, gp, tp , a0-a7, t0-t6, and s0-s11 registers */
        sw s11, 0*4(sp)
        sw s10, 1*4(sp)
        sw s9, 2*4(sp)
        sw s8, 3*4(sp)
        sw s7, 4*4(sp)
        sw s6, 5*4(sp)
        sw s5, 6*4(sp)
        sw s4, 7*4(sp)
        sw s3, 8*4(sp)
        sw s2, 9*4(sp)
        sw s1, 10*4(sp)
        sw s0, 11*4(sp)
        sw t6, 12*4(sp)
        sw t5, 13*4(sp)
        sw t4, 14*4(sp)
        sw t3, 15*4(sp)
        sw t2, 16*4(sp)
        sw t1, 17*4(sp)
        sw t0, 18*4(sp)
        sw a7, 19*4(sp)
        sw a6, 20*4(sp)
        sw a5, 21*4(sp)
        sw a4, 22*4(sp)
        sw a3, 23*4(sp)
        sw a2, 24*4(sp)
        sw a1, 25*4(sp)
        sw a0, 26*4(sp)
        sw tp, 27*4(sp)
        sw gp, 28*4(sp)
        sw ra, 29*4(sp)
        la a0, saved_regs
        csrr sp, mscratch
        sw sp, 30*4(a0)
        /* put a valid stack pointer */
        la sp, stack_end
        /* jump to panic dump C routine */
        jal report_panic
        j .

.align 2
_bss_start:
.long __bss_start
_bss_end:
.long __bss_end
_data_start:
.long __data_start
_data_end:
.long __data_end
_data_lma_start:
.long __data_lma_start

/*
 * Reserve space for system stack.
 *
 * Main routine and ISR will share this space before tasks start.
 * This space is then dedicated to ISRs after tasks start.
 *
 * NOTE: Location of system stack (.bss.system_stack) must be less than
 * tasks stacks (task_stacks@.bss) and scratchpad for first context switch
 * (scratchpad[]@.bss.task_scratchpad).
 */
.section .bss.system_stack
stack_start:
.space CONFIG_STACK_SIZE, 0
stack_end:
.global stack_end

/* sp, ra, t0, t1 registers state at exception entry */
.global excep_entry_saved_regs
excep_entry_saved_regs:
.long 0, 0, 0, 0

/* registers state at exception entry */
.global saved_regs
saved_regs:
.long 0, 0, 0, 0, 0, 0, 0, 0
.long 0, 0, 0, 0, 0, 0, 0, 0
.long 0, 0, 0, 0, 0, 0, 0, 0
.long 0, 0, 0, 0, 0, 0, 0, 0
