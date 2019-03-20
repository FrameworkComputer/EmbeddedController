/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the LM2 mIA core & interrupts
 */

#include "common.h"
#include "util.h"
#include "interrupts.h"
#include "registers.h"
#include "task_defs.h"
#include "irq_handler.h"
#include "console.h"
#include "ia_structs.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* The IDT  - initialized in init.S */
extern struct idt_entry __idt[NUM_VECTORS];

/* To count the interrupt nesting depth. Usually it is not nested */
volatile uint32_t __in_isr;

void write_ioapic_reg(const uint32_t reg, const uint32_t val)
{
	REG32(IOAPIC_IDX) = (uint8_t)reg;
	REG32(IOAPIC_WDW) = val;
}

uint32_t read_ioapic_reg(const uint32_t reg)
{
	REG32(IOAPIC_IDX) = (uint8_t)reg;
	return REG32(IOAPIC_WDW);
}

void set_ioapic_redtbl_raw(const unsigned irq, const uint32_t val)
{
	const uint32_t redtbl_lo = IOAPIC_IOREDTBL + 2 * irq;
	const uint32_t redtbl_hi = redtbl_lo + 1;

	write_ioapic_reg(redtbl_lo, val);
	write_ioapic_reg(redtbl_hi, DEST_APIC_ID);
}

/*
 * Get lower 32bit of IOAPIC redirection table entry.
 *
 * IOAPIC IRQ redirection table entry has 64 bits:
 *   bit 0-7: interrupt vector to raise on CPU
 *   bit 8-10: delivery mode, how it will send to CPU
 *   bit 11: dest mode
 *   bit 12: delivery status, 0 - idle, 1 - waiting in LAPIC
 *   bit 13: pin polarity
 *   bit 14: remote IRR
 *   bit 15: trigger mode, 0 - edge, 1 - level
 *   bit 16: mask, 0 - irq enable, 1 - irq disable
 *   bit 56-63: destination, LAPIC ID to handle this entry
 *
 * For single core system, driver should ignore higher 32bit of RTE.
 */
uint32_t get_ioapic_redtbl_lo(const unsigned int irq)
{
	return read_ioapic_reg(IOAPIC_IOREDTBL + 2 * irq);
}

void unmask_interrupt(uint32_t irq)
{
	uint32_t val;
	const uint32_t redtbl_lo = IOAPIC_IOREDTBL + 2 * irq;

	val = read_ioapic_reg(redtbl_lo);
	val &= ~IOAPIC_REDTBL_MASK;
	set_ioapic_redtbl_raw(irq, val);
}

void mask_interrupt(uint32_t irq)
{
	uint32_t val;
	const uint32_t redtbl_lo = IOAPIC_IOREDTBL + 2 * irq;

	val = read_ioapic_reg(redtbl_lo);
	val |= IOAPIC_REDTBL_MASK;
	set_ioapic_redtbl_raw(irq, val);
}

/* Maps IRQs to vectors. To be programmed in IOAPIC redirection table */
static const irq_desc_t system_irqs[] = {
	LEVEL_INTR(ISH_I2C0_IRQ, ISH_I2C0_VEC),
	LEVEL_INTR(ISH_I2C1_IRQ, ISH_I2C1_VEC),
	LEVEL_INTR(ISH_I2C2_IRQ, ISH_I2C2_VEC),
	LEVEL_INTR(ISH_WDT_IRQ, ISH_WDT_VEC),
	LEVEL_INTR(ISH_GPIO_IRQ, ISH_GPIO_VEC),
	LEVEL_INTR(ISH_IPC_HOST2ISH_IRQ, ISH_IPC_VEC),
	LEVEL_INTR(ISH_IPC_ISH2HOST_CLR_IRQ, ISH_IPC_ISH2HOST_CLR_VEC),
	LEVEL_INTR(ISH_HPET_TIMER0_IRQ, ISH_HPET_TIMER0_VEC),
	LEVEL_INTR(ISH_HPET_TIMER1_IRQ, ISH_HPET_TIMER1_VEC),
	LEVEL_INTR(ISH_DEBUG_UART_IRQ, ISH_DEBUG_UART_VEC),
	LEVEL_INTR(ISH_RESET_PREP_IRQ, ISH_RESET_PREP_VEC),
};

/**
 * The macro below is used to define 20 exeption handler routines, each
 * of which will push their corresponding interrupt vector number to
 * the stack, and then call exception_panic. The remaining arguments to
 * exception_panic were pushed by the hardware when the exception was
 * called.
 *
 * This is done since the ISR appears to be zero when exceptions are
 * handled (b:128444630). A more ideal solution would be to use
 * get_current_interrupt_vector from exception_panic. A fix for this
 * *might* involve a closer investigation of the flags passed to
 * set_interrupt_gate.
 */
#define DEFINE_EXN_HANDLER(vector)					\
	void __keep exception_panic_##vector(void);			\
	__attribute__ ((noreturn)) void exception_panic_##vector(void)	\
	{								\
		__asm__ (						\
			"push $" #vector "\n"				\
			"call exception_panic\n");			\
		while (1)						\
			continue;					\
	}

DEFINE_EXN_HANDLER(0);
DEFINE_EXN_HANDLER(1);
DEFINE_EXN_HANDLER(2);
DEFINE_EXN_HANDLER(3);
DEFINE_EXN_HANDLER(4);
DEFINE_EXN_HANDLER(5);
DEFINE_EXN_HANDLER(6);
DEFINE_EXN_HANDLER(7);
DEFINE_EXN_HANDLER(8);
DEFINE_EXN_HANDLER(9);
DEFINE_EXN_HANDLER(10);
DEFINE_EXN_HANDLER(11);
DEFINE_EXN_HANDLER(12);
DEFINE_EXN_HANDLER(13);
DEFINE_EXN_HANDLER(14);
DEFINE_EXN_HANDLER(16);
DEFINE_EXN_HANDLER(17);
DEFINE_EXN_HANDLER(18);
DEFINE_EXN_HANDLER(19);
DEFINE_EXN_HANDLER(20);

void set_interrupt_gate(uint8_t num, isr_handler_t func, uint8_t flags)
{
	uint16_t code_segment;

	/* When the flat model is used the CS will never change. */
	__asm volatile ("mov %%cs, %0":"=r" (code_segment));

	__idt[num].dword_lo = GEN_IDT_DESC_LO(func, code_segment, flags);
	__idt[num].dword_up = GEN_IDT_DESC_UP(func, code_segment, flags);
}

/**
 * This procedure gets the current interrupt vector number, and should
 * only be called from an interrupt vector context. Note that it may
 * fail under some cases (see b:128444630).
 *
 * Returns an integer in range 0-255 upon success, or 256 (0x100)
 * upon failure.
 */
uint32_t get_current_interrupt_vector(void)
{
	int i;
	uint32_t vec;

	/* In service register */
	uint32_t *ioapic_icr_last = (uint32_t *)LAPIC_ISR_REG;

	/* Scan ISRs from highest priority */
	for (i = 7; i >= 0; i--, ioapic_icr_last -= 4) {
		vec = *ioapic_icr_last;
		if (vec) {
			return (32 * i) + __fls(vec);
		}
	}

	return 0x100;
}

static uint32_t lapic_lvt_error_count;
static uint32_t ioapic_pending_count;

/*
 * Get LAPIC ISR, TMR, or IRR vector bit.
 *
 * LAPIC ISR, TMR, and IRR bit vector registers are laid out in a way that
 * skips 3 32bit word after one 32 bit entry:
 *
 *  ADDR         |  32 vectors   |    +0x4    |   +0x8    |   +0xC
 * --------------+---------------+------------+-----------+------------
 *  BASE         |  0 ~ 31       |    skip 96 bits
 * --------------+---------------+------------+-----------+------------
 *  BASE + 0x10  |  32 ~ 64      |    skip 96 bits
 * --------------+---------------+------------+-----------+------------
 *  BASE + 0x20  |  64 ~ 96      |    skip 96 bits
 * --------------+---------------+------------+-----------+------------
 *  ...
 *
 * From Kernel LAPIC driver:
 * #define VEC_POS(v) ((v) & (32 - 1))
 * #define REG_POS(v) (((v) >> 5) << 4)
 */
static inline unsigned int lapic_get_vector(uint32_t reg_base, uint32_t vector)
{
	uint32_t reg_pos = (vector >> 5) << 4;
	uint32_t vec_pos = vector & (32 - 1);

	return REG32(reg_base + reg_pos) & BIT(vec_pos);
}

/*
 * Normally, LAPIC_LVT_ERROR_VECTOR doesn't need a handler. But ISH IOAPIC
 * has an unknown bug on high frequency interrupts. A similar issue has been
 * found in PII/PIII era according to x86 APIC Kernel driver. When IOAPIC
 * routing entry is masked/unmasked at a high rate, IOAPIC line gets stuck and
 * no more interrupts are received from it.
 *
 * The solution in Kernel driver changes interrupt distribution model. But it
 * doesn't solve the problem completely. Just make it hang less frequent.
 *
 * ISH IOAPIC-LAPIC was configured in a way so we can manually send EOI (end of
 * interrupt) to IOAPIC. So in the workaround below, we ack all IOAPIC vectors
 * not in LAPIC IRR (interrupt request register). The side effect is we kicked
 * out some of the interrupts without handling them. It depends on the
 * peripheral hardware design if it re-send this irq.
 */
void handle_lapic_lvt_error(void)
{
	uint32_t esr = REG32(LAPIC_ESR_REG);
	uint32_t ioapic_redtbl, vec;
	int irq, max_irq_entries;

	/* Ack LVT ERROR exception */
	REG32(LAPIC_ESR_REG) = 0;
	lapic_lvt_error_count++;

	/*
	 * When IOAPIC has more than 1 interrupts in remote IRR state,
	 * LAPIC raises internal error.
	 */
	if (esr & LAPIC_ERR_RECV_ILLEGAL) {
		/* Scan redirect table entries */
		max_irq_entries = (read_ioapic_reg(IOAPIC_VERSION) >> 16) &
				  0xff;
		for (irq = 0; irq < max_irq_entries; irq++) {
			ioapic_redtbl = get_ioapic_redtbl_lo(irq);
			/* Skip masked IRQs */
			if (ioapic_redtbl & IOAPIC_REDTBL_MASK)
				continue;
			/* If pending interrupt is not in LAPIC, clear it. */
			if (ioapic_redtbl & IOAPIC_REDTBL_IRR) {
				vec = IRQ_TO_VEC(irq);
				if (!lapic_get_vector(LAPIC_IRR_REG, vec)) {
					/* End of interrupt */
					REG32(IOAPIC_EOI_REG) = vec;
					ioapic_pending_count++;
				}
			}
		}
	}

	CPRINTF("LAPIC error ESR:0x%02x,count:%u IOAPIC pending count:%u\n",
		esr, lapic_lvt_error_count, ioapic_pending_count);
}

/* LAPIC LVT error is not an IRQ and can not use DECLARE_IRQ() to call. */
void _lapic_error_handler(void);
__asm__ (
	".section .text._lapic_error_handler\n"
	"_lapic_error_handler:\n"
		"pusha\n"
		ASM_LOCK_PREFIX "addl $1, __in_isr\n"
		"movl %esp, %eax\n"
		"movl $stack_end, %esp\n"
		"push %eax\n"
		"call handle_lapic_lvt_error\n"
		"pop %esp\n"
		ASM_LOCK_PREFIX "subl $1, __in_isr\n"
		"popa\n"
		"iret\n"
	);

/* Should only be called in interrupt context */
void unhandled_vector(void)
{
	uint32_t vec = get_current_interrupt_vector();
	CPRINTF("Ignoring vector 0x%0x!\n", vec);
	/* Put the vector number in eax so default_int_handler can use it */
	asm("" : : "a" (vec));
}

/* This needs to be moved to link_defs.h */
extern const struct irq_data __irq_data[], __irq_data_end[];

void init_interrupts(void)
{
	unsigned entry;
	const struct irq_data *p = __irq_data;
	unsigned num_system_irqs = ARRAY_SIZE(system_irqs);
	unsigned max_entries = (read_ioapic_reg(IOAPIC_VERSION) >> 16) & 0xff;

	/* Setup gates for IRQs declared by drivers using DECLARE_IRQ */
	for (; p < __irq_data_end; p++)
		set_interrupt_gate(IRQ_TO_VEC(p->irq), p->routine, IDT_DESC_FLAGS);

	/* Setup gate for LAPIC_LVT_ERROR vector */
	set_interrupt_gate(LAPIC_LVT_ERROR_VECTOR, _lapic_error_handler,
			   IDT_DESC_FLAGS);

	/* Mask all interrupts by default in IOAPIC */
	for (entry = 0; entry < max_entries; entry++)
		set_ioapic_redtbl_raw(entry, IOAPIC_REDTBL_MASK);

	/* Enable pre-defined interrupts */
	for (entry = 0; entry < num_system_irqs; entry++)
		set_ioapic_redtbl_raw(system_irqs[entry].irq,
				      system_irqs[entry].vector |
				      IOAPIC_REDTBL_DELMOD_FIXED |
				      IOAPIC_REDTBL_DESTMOD_PHYS |
				      IOAPIC_REDTBL_MASK |
				      system_irqs[entry].polarity |
				      system_irqs[entry].trigger);

	set_interrupt_gate(ISH_TS_VECTOR, __switchto, IDT_DESC_FLAGS);

	/* Bind exception handlers to print panic message */
	set_interrupt_gate(0, exception_panic_0, IDT_DESC_FLAGS);
	set_interrupt_gate(1, exception_panic_1, IDT_DESC_FLAGS);
	set_interrupt_gate(2, exception_panic_2, IDT_DESC_FLAGS);
	set_interrupt_gate(3, exception_panic_3, IDT_DESC_FLAGS);
	set_interrupt_gate(4, exception_panic_4, IDT_DESC_FLAGS);
	set_interrupt_gate(5, exception_panic_5, IDT_DESC_FLAGS);
	set_interrupt_gate(6, exception_panic_6, IDT_DESC_FLAGS);
	set_interrupt_gate(7, exception_panic_7, IDT_DESC_FLAGS);
	set_interrupt_gate(8, exception_panic_8, IDT_DESC_FLAGS);
	set_interrupt_gate(9, exception_panic_9, IDT_DESC_FLAGS);
	set_interrupt_gate(10, exception_panic_10, IDT_DESC_FLAGS);
	set_interrupt_gate(11, exception_panic_11, IDT_DESC_FLAGS);
	set_interrupt_gate(12, exception_panic_12, IDT_DESC_FLAGS);
	set_interrupt_gate(13, exception_panic_13, IDT_DESC_FLAGS);
	set_interrupt_gate(14, exception_panic_14, IDT_DESC_FLAGS);
	set_interrupt_gate(16, exception_panic_16, IDT_DESC_FLAGS);
	set_interrupt_gate(17, exception_panic_17, IDT_DESC_FLAGS);
	set_interrupt_gate(18, exception_panic_18, IDT_DESC_FLAGS);
	set_interrupt_gate(19, exception_panic_19, IDT_DESC_FLAGS);
	set_interrupt_gate(20, exception_panic_20, IDT_DESC_FLAGS);

	/* Note: At reset, ID field is already set to 0 in APIC ID register */

	/* Enable the APIC, mapping the spurious interrupt at the same time. */
	APIC_SPURIOUS_INT = LAPIC_SPURIOUS_INT_VECTOR | APIC_ENABLE_BIT;

	/* Set timer error vector. */
	APIC_LVT_ERROR = LAPIC_LVT_ERROR_VECTOR;
}
