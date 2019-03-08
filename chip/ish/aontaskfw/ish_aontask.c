/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * ISH aontask is a seprated very small program from main FW, not like main FW
 * resides in main SRAM, aontask resides in a small AON memory (ISH3 has no
 * seprated AON memory, reserved last 4KB of main SRAM for AON use, from ISH4,
 * there is seprated AON memory, 4KB for ISH4, and 8KB for ISH5).
 *
 * When ISH entered into low power states, aontask may get switched and run,
 * aontask managments the main SRAM and is responsible for store and restore
 * main FW's running context, for example, when entering D0i2 state, put main
 * SRAM into retention mode, when exit D0i2 state and before switch back to
 * main FW, put main SRAM into normal access mode, when entering D0i3 state,
 * at first stores main FW's writeable data into IMR DDR (read only code and
 * data already have copies in IMR), then power off the main SRAM completely,
 * when exit D0i3 state, at first power on the main SRAM, and restore main FW's
 * code and data from IMR to main SRAM, then switch back to main FW.
 *
 * On ISH, except the hpet timer, also have other wakeup sources, peripheral
 * events, such as gpio interrupt, uart interrupt, ipc interrupt, I2C and SPI
 * access are also can wakeup ISH. ISH's PMU (power management unit HW) will
 * manage these wakeup sources and transfer to a PMU wakeup interrupt which
 * can wakeup aontask, and aontask will handle it, when aontask got up, and
 * swiched back to main FW, main FW will receive the original wakeup source
 * interrupt which triggered the PMU wakeup interrupt in aontask, then main FW
 * handle the original interrupt normally.
 *
 * In most of the time, aontask is in halt state, and waiting for PMU wakeup
 * interrupt to wakeup (reset prep interrupt also can wakeup aontask
 * if CONFIG_ISH_PM_RESET_PREP defined), after wakeup, aontask will handle the
 * low power states exit process and finaly switch back to main FW.
 *
 * aontask is running in the 32bit protection mode with flat memory segment
 * settings, paging and cache are disabled (cache will be power gated).
 *
 * We use x86's hardware context switching mechanism for the switching of
 * main FW and aontask.
 * see	https://wiki.osdev.org/Context_Switching
 *      https://en.wikipedia.org/wiki/Task_state_segment
 *
 */

#include <common.h>
#include <ia_structs.h>
#include "power_mgt.h"
#include "ish_aon_share.h"

/**
 * ISH aontask only need handle PMU wakeup interrupt and reset prep interrupt
 * (if CONFIG_ISH_PM_RESET_PREP defined), before switch to aontask, all other
 * interrupts should be masked. Since aontask is a seprated program from
 * main FW, and the main SRAM will be power offed or will be put in in
 * retention mode, aontask need its own IDT to handle PMU wakeup interrupt and
 * reset prep interrupt (if CONFIG_ISH_PM_RESET_PREP defined)
 *
 * Due to very limit AON memory size (typically total 4KB), we don't want to
 * define and allocate whole 256 entries for aontask'IDT, that almost need 2KB
 * (256 * 8), so we just defined the only needed IDT entries:
 * AON_IDT_ENTRY_VEC_FIRST ~  AON_IDT_ENTRY_VEC_LAST
 */
#define AON_IDT_ENTRY_VEC_FIRST        ISH_PMU_WAKEUP_VEC

#ifdef CONFIG_ISH_PM_RESET_PREP
/**
 * assume reset prep interrupt vector is greater than PMU wakeup interrupt
 * vector, and also need handle reset prep interrupt
 * (if CONFIG_ISH_PM_RESET_PREP defined)
 */
#define AON_IDT_ENTRY_VEC_LAST         ISH_RESET_PREP_VEC
#else
/* only need handle single PMU wakeup interrupt */
#define AON_IDT_ENTRY_VEC_LAST         ISH_PMU_WAKEUP_VEC
#endif

static void handle_reset(void);

/* ISR for PMU wakeup interrupt */
static void pmu_wakeup_isr(void)
{
	/**
	 * Indicate completion of servicing the interrupt to IOAPIC first
	 * then indicate completion of servicing the interrupt to LAPIC
	 */
	REG32(IOAPIC_EOI_REG) = ISH_PMU_WAKEUP_VEC;
	REG32(LAPIC_EOI_REG) = 0x0;

	__asm__ volatile ("iret;");

	__builtin_unreachable();
}

#ifdef CONFIG_ISH_PM_RESET_PREP

/* ISR for reset prep interrupt */
static void reset_prep_isr(void)
{
	/* mask reset prep avail interrupt */
	PMU_RST_PREP = PMU_RST_PREP_INT_MASK;

	/**
	 * Indicate completion of servicing the interrupt to IOAPIC first
	 * then indicate completion of servicing the interrupt to LAPIC
	 */
	REG32(IOAPIC_EOI_REG) = ISH_RESET_PREP_VEC;
	REG32(LAPIC_EOI_REG) = 0x0;

	handle_reset();

	__builtin_unreachable();
}

#endif


/**
 * Use a static data array for aon IDT, and setting IDT header for IDTR
 * register
 *
 * Due to very limit AON memory size (typically total 4KB), we don't want to
 * define and allocate whole 256 entries for aontask'IDT, that almost need 2KB
 * (256 * 8), so we just defined the only needed IDT entries:
 * AON_IDT_ENTRY_VEC_FIRST ~  AON_IDT_ENTRY_VEC_LAST
 *
 * Since on x86, the IDT entry index (count from 0) is also the interrupt
 * vector number, for IDT header, the 'start' filed still need to point to
 * the entry 0, and 'size' must count from entry 0.
 *
 * We only allocated memory for entry AON_IDT_ENTRY_VEC_FIRST to
 * AON_IDT_ENTRY_VEC_LAST, a little trick, but works well on ISH
 *
 *              ------>---------------------------<----- aon_idt_hdr.start
 *                |    |          entry 0        |
 *                |    +-------------------------+
 *                |    |           ...           |
 *                |    +-------------------------+<-----
 *  aon_idt_hdr.size   | AON_IDT_ENTRY_VEC_FIRST |    |
 *                |    +-------------------------+    |
 *                |    |            ...          | allocated memory in aon_idt
 *                |    +-------------------------+    |
 *                |    | AON_IDT_ENTRY_VEC_LAST  |    |
 *              ------>+-------------------------+<-----
 *                     |            ...          |
 *                     +-------------------------+
 *                     |          entry 255      |
 *                     ---------------------------
 */

static struct idt_entry aon_idt[AON_IDT_ENTRY_VEC_LAST -
				AON_IDT_ENTRY_VEC_FIRST + 1];

static struct idt_header aon_idt_hdr = {

	.limit = (sizeof(struct idt_entry) * (AON_IDT_ENTRY_VEC_LAST + 1)) - 1,
	.entries = (struct idt_entry *)((uint32_t)&aon_idt -
			(sizeof(struct idt_entry) * AON_IDT_ENTRY_VEC_FIRST))
};

/* aontask entry point function */
void ish_aon_main(void);

/**
 * 8 bytes reserved on stack, just for GDB to show the correct stack
 * information when doing source code level debuging
 */
#define AON_SP_RESERVED (8)

/* TSS segment for aon task */
static struct tss_entry aon_tss = {
	.prev_task_link = 0,
	.reserved1 = 0,
	.esp0 = (uint8_t *)(CONFIG_ISH_AON_SRAM_ROM_START - AON_SP_RESERVED),
	/* entry 1 in LDT for data segment */
	.ss0 = 0xc,
	.reserved2 = 0,
	.esp1 = 0,
	.ss1 = 0,
	.reserved3 = 0,
	.esp2 = 0,
	.ss2 = 0,
	.reserved4 = 0,
	.cr3 = 0,
	/* task excute entry point */
	.eip = (uint32_t)&ish_aon_main,
	.eflags = 0,
	.eax = 0,
	.ecx = 0,
	.edx = 0,
	.ebx = 0,
	/* set stack top pointer at the end of usable aon memory */
	.esp = CONFIG_ISH_AON_SRAM_ROM_START - AON_SP_RESERVED,
	.ebp = AON_SP_RESERVED,
	.esi = 0,
	.edi = 0,
	/* entry 1 in LDT for data segment */
	.es = 0xc,
	.reserved5 = 0,
	/* entry 0 in LDT for code segment */
	.cs = 0x4,
	.reserved6 = 0,
	/* entry 1 in LDT for data segment */
	.ss = 0xc,
	.reserved7 = 0,
	/* entry 1 in LDT for data segment */
	.ds = 0xc,
	.reserved8 = 0,
	/* entry 1 in LDT for data segment */
	.fs = 0xc,
	.reserved9 = 0,
	/* entry 1 in LDT for data segment */
	.gs = 0xc,
	.reserved10 = 0,
	.ldt_seg_selector = 0,
	.reserved11 = 0,
	.trap_debug = 0,

	/**
	 * TSS's limit specified as 0x67, to allow the task has permission to
	 * access I/O port using IN/OUT instructions,'iomap_base_addr' field
	 * must be greater than or equal to TSS' limit
	 * see 'I/O port permissions' on
	 *	https://en.wikipedia.org/wiki/Task_state_segment
	 */
	.iomap_base_addr = GDT_DESC_TSS_LIMIT
};

/**
 * define code and data LDT segements for aontask
 * code : base = 0x0, limit = 0xFFFFFFFF, Present = 1, DPL = 0
 * data : base = 0x0, limit = 0xFFFFFFFF, Present = 1, DPL = 0
 */
static ldt_entry aon_ldt[2] = {

	/**
	 * entry 0 for code segment
	 * base: 0x0
	 * limit: 0xFFFFFFFF
	 * flag: 0x9B, Present = 1, DPL = 0, code segment
	 */
	{
		.dword_lo = GEN_GDT_DESC_LO(0x0, 0xFFFFFFFF,
				GDT_DESC_CODE_FLAGS),

		.dword_up = GEN_GDT_DESC_UP(0x0, 0xFFFFFFFF,
				GDT_DESC_CODE_FLAGS)
	},

	/**
	 * entry 1 for data segment
	 * base: 0x0
	 * limit: 0xFFFFFFFF
	 * flag: 0x93, Present = 1, DPL = 0, data segment
	 */
	{
		.dword_lo = GEN_GDT_DESC_LO(0x0, 0xFFFFFFFF,
				GDT_DESC_DATA_FLAGS),

		.dword_up = GEN_GDT_DESC_UP(0x0, 0xFFFFFFFF,
				GDT_DESC_DATA_FLAGS)
	}
};


/* shared data structure between main FW and aon task */
struct ish_aon_share aon_share = {
	.magic_id = AON_MAGIC_ID,
	.error_count = 0,
	.last_error = AON_SUCCESS,
	.aon_tss = &aon_tss,
	.aon_ldt = &aon_ldt[0],
	.aon_ldt_size = sizeof(aon_ldt),
};

static void handle_d0i2(void)
{
	/* TODO set main SRAM into retention mode*/

	/* ish_halt(); */
	/* wakeup from PMU interrupt */

	/* TODO set main SRAM intto normal mode */
}

static void handle_d0i3(void)
{
	/* TODO store main FW 's context to IMR DDR from main SRAM */
	/* TODO power off main SRAM */

	/* ish_halt(); */
	/* wakeup from PMU interrupt */

	/* TODO power on main SRAM */
	/* TODO restore main FW 's context to main SRAM from IMR DDR */
}

static void handle_d3(void)
{
	/* TODO store main FW 's context to IMR DDR from main sram */
	/* TODO power off main SRAM */

	/* TODO handle D3 */
}

static void handle_reset(void)
{
	/* TODO power off main SRAM */
	/* TODO handle reset */
}

static void handle_unknown_state(void)
{
	aon_share.last_error = AON_ERROR_NOT_SUPPORT_POWER_MODE;
	aon_share.error_count++;

	/* switch back to main FW */
}

void ish_aon_main(void)
{

	/* set PMU wakeup interrupt gate using LDT code segment selector(0x4) */
	aon_idt[0].dword_lo = GEN_IDT_DESC_LO(&pmu_wakeup_isr, 0x4,
					IDT_DESC_FLAGS);

	aon_idt[0].dword_up = GEN_IDT_DESC_UP(&pmu_wakeup_isr, 0x4,
					IDT_DESC_FLAGS);
#ifdef CONFIG_ISH_PM_RESET_PREP

	/* set reset prep interrupt gate using LDT code segment selector(0x4) */
	aon_idt[AON_IDT_ENTRY_VEC_LAST - AON_IDT_ENTRY_VEC_FIRST].dword_lo =
			GEN_IDT_DESC_LO(&reset_prep_isr, 0x4, IDT_DESC_FLAGS);

	aon_idt[AON_IDT_ENTRY_VEC_LAST - AON_IDT_ENTRY_VEC_FIRST].dword_up =
			GEN_IDT_DESC_UP(&reset_prep_isr, 0x4, IDT_DESC_FLAGS);
#endif

	while (1) {

		/**
		 *  will start to run from here when switched to aontask from
		 *  the second time
		 */

		/* save main FW's IDT and load aontask's IDT */
		__asm__ volatile (
				"sidtl %0;\n"
				"lidtl %1;\n"
				:
				: "m" (aon_share.main_fw_idt_hdr),
				  "m" (aon_idt_hdr)
				);

		aon_share.last_error = AON_SUCCESS;

		switch (aon_share.pm_state) {
		case ISH_PM_STATE_D0I2:
			handle_d0i2();
			break;
		case ISH_PM_STATE_D0I3:
			handle_d0i3();
			break;
		case ISH_PM_STATE_D3:
			handle_d3();
			break;
		case ISH_PM_STATE_RESET_PREP:
			handle_reset();
			break;
		default:
			handle_unknown_state();
			break;
		}

		/* restore main FW's IDT and switch back to main FW */
		__asm__ volatile(
				"lidtl %0;\n"
				"iret;"
				:
				: "m" (aon_share.main_fw_idt_hdr)
				);
	}
}
