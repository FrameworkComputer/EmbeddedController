/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers map and definitions for mIA LM2 processor
 */

#ifndef __CROS_EC_IA32_INTERRUPTS_H
#define __CROS_EC_IA32_INTERRUPTS_H

#ifndef __ASSEMBLER__
#include <stdint.h>

#define USHRT_MAX		0xFFFF
typedef struct {
	unsigned irq;
	unsigned trigger;
	unsigned polarity;
	unsigned vector;
} irq_desc_t;

#define INTR_DESC(__irq,__vector,__trig)                    \
    {                                                       \
        .irq            = __irq,                            \
        .trigger        = __trig,                           \
        .polarity       = IOAPIC_REDTBL_INTPOL_HIGH,        \
        .vector         = __vector	                    \
    }

#define LEVEL_INTR(__irq, __vector) \
	INTR_DESC(__irq, __vector, IOAPIC_REDTBL_TRIGGER_LEVEL)
#define EDGE_INTR(__irq, __vector) \
	INTR_DESC(__irq, __vector, IOAPIC_REDTBL_TRIGGER_EDGE)
#endif

/* ISH has a single core processor */
#define DEST_APIC_ID			0
#define NUM_VECTORS			256

/* APIC bit definitions. */
#define APIC_DIV_16			0x03
#define APIC_ENABLE_BIT			(1UL << 8UL)
#define APIC_SPURIOUS_INT		REG32(ISH_LAPIC_BASE + 0xF0UL )
#define APIC_LVT_ERROR			REG32(ISH_LAPIC_BASE + 0x370UL)

#ifndef __ASSEMBLER__

typedef void (*isr_handler_t) (void);

void init_interrupts(void);
void mask_interrupt(unsigned int irq);
void unmask_interrupt(unsigned int irq);

/**
 * disable current all enabled intrrupts
 * return current irq mask bitmap
 * power management typically use 'disable_all_interrupts' to disable current
 * all interrupts and save current interrupts enabling settings before enter
 * low power state, and use 'restore_interrupts' to restore the interrupts
 * settings after exit low power state.
 */
uint64_t disable_all_interrupts(void);
void restore_interrupts(uint64_t irq_map);

/* Only call in interrupt context */
uint32_t get_current_interrupt_vector(void);
#endif

#endif	/* __CROS_EC_IA32_INTERRUPTS_H */
