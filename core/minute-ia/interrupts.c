/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the LM2 mIA core & interrupts
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "ia_structs.h"
#include "interrupts.h"
#include "irq_handler.h"
#include "link_defs.h"
#include "mia_panic_internal.h"
#include "registers.h"
#include "task.h"
#include "task_defs.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* The IDT  - initialized in init.S */
extern struct idt_entry __idt[NUM_VECTORS];

/* To count the interrupt nesting depth. Usually it is not nested */
volatile uint32_t __in_isr;

static void write_ioapic_reg(const uint32_t reg, const uint32_t val)
{
	IOAPIC_IDX = reg;
	IOAPIC_WDW = val;
}

static uint32_t read_ioapic_reg(const uint32_t reg)
{
	IOAPIC_IDX = reg;
	return IOAPIC_WDW;
}

static void set_ioapic_redtbl_raw(const uint32_t irq, const uint32_t val)
{
	const uint32_t redtbl_lo = IOAPIC_IOREDTBL + 2 * irq;
	const uint32_t redtbl_hi = redtbl_lo + 1;

	write_ioapic_reg(redtbl_lo, val);
	write_ioapic_reg(redtbl_hi, DEST_APIC_ID);
}

/**
 * bitmap for current IRQ's mask status
 * ISH support max 64 IRQs, 64 bit bitmap value is ok
 */
#define ISH_MAX_IOAPIC_IRQS (64)
uint64_t ioapic_irq_mask_bitmap;

/**
 * disable current all enabled intrrupts
 * return current irq mask bitmap
 * power management typically use 'disable_all_interrupts' to disable current
 * all interrupts and save current interrupts enabling settings before enter
 * low power state, and use 'restore_interrupts' to restore the interrupts
 * settings after exit low power state.
 */
uint64_t disable_all_interrupts(void)
{
	uint64_t saved_map;
	int i;

	saved_map = ioapic_irq_mask_bitmap;

	for (i = 0; i < ISH_MAX_IOAPIC_IRQS; i++) {
		if (((uint64_t)0x1 << i) & saved_map)
			mask_interrupt(i);
	}

	return saved_map;
}

void restore_interrupts(uint64_t irq_map)
{
	int i;

	/* Disable interrupts until everything is unmasked */
	interrupt_disable();
	for (i = 0; i < ISH_MAX_IOAPIC_IRQS; i++) {
		if (((uint64_t)0x1 << i) & irq_map)
			unmask_interrupt(i);
	}
	interrupt_enable();
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
	ioapic_irq_mask_bitmap |= ((uint64_t)0x1) << irq;
}

void mask_interrupt(uint32_t irq)
{
	uint32_t val;
	const uint32_t redtbl_lo = IOAPIC_IOREDTBL + 2 * irq;

	val = read_ioapic_reg(redtbl_lo);
	val |= IOAPIC_REDTBL_MASK;
	set_ioapic_redtbl_raw(irq, val);

	ioapic_irq_mask_bitmap &= ~(((uint64_t)0x1) << irq);
}

/* Maps IRQs to vectors. To be programmed in IOAPIC redirection table */
static const irq_desc_t system_irqs[] = {
	LEVEL_INTR(ISH_I2C0_IRQ, ISH_I2C0_VEC),
	LEVEL_INTR(ISH_I2C1_IRQ, ISH_I2C1_VEC),
	LEVEL_INTR(ISH_I2C2_IRQ, ISH_I2C2_VEC),
	LEVEL_INTR(ISH_WDT_IRQ, ISH_WDT_VEC),
	LEVEL_INTR(ISH_GPIO_IRQ, ISH_GPIO_VEC),
	LEVEL_INTR(ISH_IPC_HOST2ISH_IRQ, ISH_IPC_VEC),
#ifndef CONFIG_ISH_HOST2ISH_COMBINED_ISR
	LEVEL_INTR(ISH_IPC_ISH2HOST_CLR_IRQ, ISH_IPC_ISH2HOST_CLR_VEC),
#endif
	LEVEL_INTR(ISH_HPET_TIMER1_IRQ, ISH_HPET_TIMER1_VEC),
	LEVEL_INTR(ISH_DEBUG_UART_IRQ, ISH_DEBUG_UART_VEC),
	LEVEL_INTR(ISH_FABRIC_IRQ, ISH_FABRIC_VEC),
#ifdef CONFIG_ISH_PM_RESET_PREP
	LEVEL_INTR(ISH_RESET_PREP_IRQ, ISH_RESET_PREP_VEC),
#endif
#ifdef CONFIG_ISH_PM_D0I1
	LEVEL_INTR(ISH_PMU_WAKEUP_IRQ, ISH_PMU_WAKEUP_VEC),
#endif
#ifdef CONFIG_ISH_PM_D3
	LEVEL_INTR(ISH_D3_RISE_IRQ, ISH_D3_RISE_VEC),
#ifndef CONFIG_ISH_NEW_PM
	LEVEL_INTR(ISH_D3_FALL_IRQ, ISH_D3_FALL_VEC),
	LEVEL_INTR(ISH_BME_RISE_IRQ, ISH_BME_RISE_VEC),
	LEVEL_INTR(ISH_BME_FALL_IRQ, ISH_BME_FALL_VEC)
#endif
#endif
};

/**
 * The macro below is used to define 20 exeption handler routines, each
 * of which will push their corresponding interrupt vector number to
 * the stack, and then call exception_panic. The remaining arguments to
 * exception_panic were pushed by the hardware when the exception was
 * called.  The errorcode is pushed to the stack by hardware for vectors
 * 8, 10-14 and 17.  Make sure to push 0 for the other vectors
 *
 * This is done since interrupt vectors 0-31 bypass the APIC ISR register
 * and go directly to the CPU core, so get_current_interrupt_vector
 * cannot be used.
 */
#define DEFINE_EXN_HANDLER(vector) \
	_DEFINE_EXN_HANDLER(vector, exception_panic_##vector)
#define _DEFINE_EXN_HANDLER(vector, name)          \
	void __keep name(void);                    \
	__noreturn void name(void)                 \
	{                                          \
		__asm__("push $0\n"                \
			"push $" #vector "\n"      \
			"call exception_panic\n"); \
		__builtin_unreachable();           \
	}

#define DEFINE_EXN_HANDLER_W_ERRORCODE(vector) \
	_DEFINE_EXN_HANDLER_W_ERRORCODE(vector, exception_panic_##vector)
#define _DEFINE_EXN_HANDLER_W_ERRORCODE(vector, name) \
	void __keep name(void);                       \
	__noreturn void name(void)                    \
	{                                             \
		__asm__("push $" #vector "\n"         \
			"call exception_panic\n");    \
		__builtin_unreachable();              \
	}

DEFINE_EXN_HANDLER(0);
DEFINE_EXN_HANDLER(1);
DEFINE_EXN_HANDLER(2);
DEFINE_EXN_HANDLER(3);
DEFINE_EXN_HANDLER(4);
DEFINE_EXN_HANDLER(5);
DEFINE_EXN_HANDLER(6);
DEFINE_EXN_HANDLER(7);
DEFINE_EXN_HANDLER_W_ERRORCODE(8);
DEFINE_EXN_HANDLER(9);
DEFINE_EXN_HANDLER_W_ERRORCODE(10);
DEFINE_EXN_HANDLER_W_ERRORCODE(11);
DEFINE_EXN_HANDLER_W_ERRORCODE(12);
DEFINE_EXN_HANDLER_W_ERRORCODE(13);
DEFINE_EXN_HANDLER_W_ERRORCODE(14);
DEFINE_EXN_HANDLER(16);
DEFINE_EXN_HANDLER_W_ERRORCODE(17);
DEFINE_EXN_HANDLER(18);
DEFINE_EXN_HANDLER(19);
DEFINE_EXN_HANDLER(20);

/**
 * Use a similar approach for defining an optional handler for
 * watchdog timer expiration. However, this time, hardware does not
 * push errorcode, and we must account for that by pushing zero.
 */
__noreturn __keep void exception_panic_wdt(uint32_t cs)
{
	exception_panic(CONFIG_MIA_WDT_VEC, 0,
			(uint32_t)__builtin_return_address(0), cs, 0);
}

void set_interrupt_gate(uint8_t num, isr_handler_t func, uint8_t flags)
{
	uint16_t code_segment;

	/* When the flat model is used the CS will never change. */
	__asm volatile("mov %%cs, %0" : "=r"(code_segment));

	__idt[num].dword_lo = GEN_IDT_DESC_LO(func, code_segment, flags);
	__idt[num].dword_up = GEN_IDT_DESC_UP(func, code_segment, flags);
}

/**
 * This procedure gets the current interrupt vector number using the
 * APIC ISR register, and should only be called from an interrupt
 * vector context. Note that vectors 0-31, as well as software
 * triggered interrupts (using "int n") bypass the APIC, and this
 * routine will not work for that.
 *
 * Returns an integer in range 0-255 upon success, or 256 (0x100)
 * upon failure.
 */
uint32_t get_current_interrupt_vector(void)
{
	int i;
	uint32_t vec;

	/* In service register */
	volatile uint32_t *ioapic_isr_last = &LAPIC_ISR_LAST_REG;

	/* Scan ISRs from highest priority */
	for (i = 7; i >= 0; i--, ioapic_isr_last -= 4) {
		vec = *ioapic_isr_last;
		if (vec) {
			return (32 * i) + __fls(vec);
		}
	}

	return 0x100;
}

static uint32_t lapic_lvt_error_count;
static uint32_t ioapic_pending_count;
static uint32_t last_esr;

static void print_lpaic_lvt_error(void)
{
	CPRINTS("LAPIC error ESR 0x%02x: %u; IOAPIC pending: %u", last_esr,
		lapic_lvt_error_count, ioapic_pending_count);
}
DECLARE_DEFERRED(print_lpaic_lvt_error);

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
static inline unsigned int lapic_get_vector(volatile uint32_t *reg_base,
					    uint32_t vector)
{
	/*
	 * Since we are using array indexing, we need to divide the vec_pos by
	 * sizeof(uint32_t), i.e. shift to the right 2.
	 */
	uint32_t reg_pos = (vector >> 5) << 2;
	uint32_t vec_pos = vector & (32 - 1);

	return reg_base[reg_pos] & BIT(vec_pos);
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
	uint32_t esr = LAPIC_ESR_REG;
	uint32_t ioapic_redtbl, vec;
	int irq, max_irq_entries;

	/* Ack LVT ERROR exception */
	LAPIC_ESR_REG = 0;

	/*
	 * When IOAPIC has more than 1 interrupts in remote IRR state,
	 * LAPIC raises internal error.
	 */
	if (esr & LAPIC_ERR_RECV_ILLEGAL) {
		lapic_lvt_error_count++;

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
				if (!lapic_get_vector(&LAPIC_IRR_REG, vec)) {
					/* End of interrupt */
					IOAPIC_EOI_REG = vec;
					ioapic_pending_count++;
				}
			}
		}
	}

	if (esr) {
		/* Don't print in interrupt context because it is too slow */
		last_esr = esr;
		hook_call_deferred(&print_lpaic_lvt_error_data, 0);
	}
}

/* LAPIC LVT error is not an IRQ and can not use DECLARE_IRQ() to call. */
void _lapic_error_handler(void);
__asm__(".section .text._lapic_error_handler\n"
	"_lapic_error_handler:\n"
	"pusha\n" ASM_LOCK_PREFIX "addl $1, __in_isr\n"
	"movl %esp, %eax\n"
	"movl $stack_end, %esp\n"
	"push %eax\n"
#ifdef CONFIG_TASK_PROFILING
	"push $" STRINGIFY(
		CONFIG_IRQ_COUNT) "\n"
				  "call task_start_irq_handler\n"
				  "addl $0x04, %esp\n"
#endif
				  "call handle_lapic_lvt_error\n"
				  "pop %esp\n"
				  "movl $0x00, (0xFEE000B0)\n" /* Set
								  EOI
								  for
								  LAPIC
								*/
	ASM_LOCK_PREFIX "subl $1, __in_isr\n"
				  "popa\n"
				  "iret\n");

/* Should only be called in interrupt context */
void unhandled_vector(void)
{
	uint32_t vec = get_current_interrupt_vector();
	CPRINTF("Ignoring vector 0x%0x!\n", vec);
	/* Put the vector number in eax so default_int_handler can use it */
	asm("" : : "a"(vec));
}

/**
 * Called from SOFTIRQ_VECTOR when software is trigger an IRQ manually
 *
 * If IRQ is out of range, then no routine should be called
 */
void call_irq_service_routine(uint32_t irq)
{
	const struct irq_def *p = __irq_data;

	/* If just rescheduling a task, we won't have a routine to call */
	if (irq >= CONFIG_IRQ_COUNT)
		return;

	for (; p < __irq_data_end; p++) {
		if (p->irq == irq) {
			p->routine();
			break;
		}
	}

	if (p == __irq_data_end)
		CPRINTS("IRQ %d routine not found!", irq);
}

void lapic_restore(void)
{
	LAPIC_ESR_REG = 0;
	APIC_SPURIOUS_INT = LAPIC_SPURIOUS_INT_VECTOR | APIC_ENABLE_BIT;
	APIC_LVT_ERROR = LAPIC_LVT_ERROR_VECTOR;
}

void init_interrupts(void)
{
	unsigned int entry;
	const struct irq_def *p;
	unsigned int num_system_irqs = ARRAY_SIZE(system_irqs);
	unsigned int max_entries = (read_ioapic_reg(IOAPIC_VERSION) >> 16) &
				   0xff;

	/* Setup gates for IRQs declared by drivers using DECLARE_IRQ */
	for (p = __irq_data; p < __irq_data_end; p++)
		set_interrupt_gate(IRQ_TO_VEC(p->irq), p->handler,
				   IDT_DESC_FLAGS);

	/* Software generated IRQ */
	set_interrupt_gate(SOFTIRQ_VECTOR, sw_irq_handler, IDT_DESC_FLAGS);

	/* Setup gate for LAPIC_LVT_ERROR vector; clear any remnant error. */
	LAPIC_ESR_REG = 0;
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

	/*
	 * Set up watchdog expiration like a panic, that way we can
	 * use the common panic handling code, and also properly
	 * retrieve EIP.
	 */
	if (IS_ENABLED(CONFIG_WATCHDOG))
		set_interrupt_gate(CONFIG_MIA_WDT_VEC,
				   (isr_handler_t)exception_panic_wdt,
				   IDT_DESC_FLAGS);

	/* Note: At reset, ID field is already set to 0 in APIC ID register */

	/* Enable the APIC, mapping the spurious interrupt at the same time. */
	APIC_SPURIOUS_INT = LAPIC_SPURIOUS_INT_VECTOR | APIC_ENABLE_BIT;

	/* Set timer error vector. */
	APIC_LVT_ERROR = LAPIC_LVT_ERROR_VECTOR;
}
