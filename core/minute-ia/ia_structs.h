/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IA_STRUCTS_H
#define __CROS_EC_IA_STRUCTS_H

#ifndef __ASSEMBLER__

#include "common.h"

/**
 * IA32/x86 architecture related data structure definitions.
 * including: Global Descriptor Table (GDT), Local Descriptor Table (LDT),
 * Interrupt Descriptor Table (IDT) and Task State Segment (TSS)
 * see: https://en.wikipedia.org/wiki/Global_Descriptor_Table
 *      https://en.wikipedia.org/wiki/Interrupt_descriptor_table
 *      https://en.wikipedia.org/wiki/Task_state_segment
 */

/* GDT entry descriptor */
struct gdt_entry {
	union {
		struct {
			uint32_t dword_lo; /* lower dword */
			uint32_t dword_up; /* upper dword */
		};
		struct {
			uint16_t limit_lw; /* limit (0:15) */
			uint16_t base_addr_lw; /* base address (0:15) */
			uint8_t base_addr_mb; /* base address (16:23) */
			uint8_t flags; /* flags */
			uint8_t limit_ub; /* limit (16:19) */
			uint8_t base_addr_ub; /* base address (24:31) */
		};
	};

} __packed;

typedef struct gdt_entry ldt_entry;

/* GDT header */
struct gdt_header {
	uint16_t limit; /* GDT limit size */
	struct gdt_entry *entries; /* pointer to GDT entries */
} __packed;

/* IDT entry descriptor */
struct idt_entry {
	union {
		struct {
			uint32_t dword_lo; /* lower dword */
			uint32_t dword_up; /* upper dword */
		};

		struct {
			uint16_t offset_lw; /* offset (0:15) */
			uint16_t seg_selector; /* segment selector */
			uint8_t zero; /* must be set to zero */
			uint8_t flags; /* flags */
			uint16_t offset_uw; /* offset (16:31) */
		};
	};
} __packed;

/* IDT header */
struct idt_header {
	uint16_t limit; /* IDT limit size */
	struct idt_entry *entries; /* pointer to IDT entries */
} __packed;

/* TSS entry descriptor */
struct tss_entry {
	uint16_t prev_task_link;
	uint16_t reserved1;
	uint8_t *esp0;
	uint16_t ss0;
	uint16_t reserved2;
	uint8_t *esp1;
	uint16_t ss1;
	int16_t reserved3;
	uint8_t *esp2;
	uint16_t ss2;
	uint16_t reserved4;
	uint32_t cr3;
	int32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	int32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	int16_t es;
	uint16_t reserved5;
	uint16_t cs;
	uint16_t reserved6;
	uint16_t ss;
	uint16_t reserved7;
	uint16_t ds;
	uint16_t reserved8;
	uint16_t fs;
	uint16_t reserved9;
	uint16_t gs;
	uint16_t reserved10;
	uint16_t ldt_seg_selector;
	uint16_t reserved11;
	uint16_t trap_debug;
	/* offset from TSS base for I/O perms */
	uint16_t iomap_base_addr;
} __packed;

#endif

/* code segment flag,  E/R, Present = 1, DPL = 0, Acesssed = 1 */
#define GDT_DESC_CODE_FLAGS (0x9B)

/* data segment flag,  R/W, Present = 1, DPL = 0, Acesssed = 1 */
#define GDT_DESC_DATA_FLAGS (0x93)

/* TSS segment limit size */
#define GDT_DESC_TSS_LIMIT (0x67)

/* TSS segment flag, Present = 1, DPL = 0, Acesssed = 1 */
#define GDT_DESC_TSS_FLAGS (0x89)

/* LDT segment flag, Present = 1, DPL = 0 */
#define GDT_DESC_LDT_FLAGS (0x82)

/* IDT descriptor flag, Present = 1, DPL = 0, 32-bit interrupt gate */
#define IDT_DESC_FLAGS (0x8E)

/**
 * macros helper to create a GDT entry descriptor
 * set default 4096-byte pages for granularity
 * base: 32bit base address
 * limit: 32bit limit size of bytes (will covert to unit of 4096-byte pages)
 * flags: 8bit flags
 */
#define GEN_GDT_DESC_LO(base, limit, flags) \
	((((limit) >> 12) & 0xFFFF) | (((base) & 0xFFFF) << 16))

#define GEN_GDT_DESC_UP(base, limit, flags)                    \
	((((base) >> 16) & 0xFF) | (((flags) << 8) & 0xFF00) | \
	 (((limit) >> 12) & 0xFF0000) | ((base) & 0xFF000000) | 0xc00000)

/**
 * macro helper to create a IDT entry descriptor
 */
#define GEN_IDT_DESC_LO(offset, selector, flags) \
	(((uint32_t)(offset) & 0xFFFF) | (((selector) & 0xFFFF) << 16))

#define GEN_IDT_DESC_UP(offset, selector, flags) \
	(((uint32_t)(offset) & 0xFFFF0000) | (((flags) & 0xFF) << 8))

#endif /* __CROS_EC_IA_STRUCTS_H */
