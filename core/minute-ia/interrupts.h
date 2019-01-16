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

/* Default flags setting for entries in the IDT.
 * 7	- Present bit
 * 6:5	- Descriptor privilege level
 * 4	- Storage segment (0 for interrupt gate)
 * 3:0	- Gate type (1110 = Interrupt gate)
 */
#define IDT_FLAGS			0x8E

/* APIC bit definitions. */
#define APIC_DIV_16			0x03
#define APIC_ENABLE_BIT			(1UL << 8UL)
#define APIC_SPURIOUS_INT		REG32(ISH_LAPIC_BASE + 0xF0UL )
#define APIC_LVT_ERROR			REG32(ISH_LAPIC_BASE + 0x370UL)

#ifndef __ASSEMBLER__
/* Interrupt descriptor entry */
struct IDT_entry_t {
	uint16_t ISR_low;	/* Low 16 bits of handler address. */
	uint16_t segment_selector;	/* Flat model means this is not changed. */
	uint8_t zero;		/* Must be set to zero. */
	uint8_t flags;		/* Flags for this entry. */
	uint16_t ISR_high;	/* High 16 bits of handler address. */
} __attribute__ ((packed));
typedef struct IDT_entry_t IDT_entry;

typedef void (*isr_handler_t) (void);

void init_interrupts(void);
void mask_interrupt(unsigned int irq);
void unmask_interrupt(unsigned int irq);
#endif

#endif	/* __CROS_EC_IA32_INTERRUPTS_H */
