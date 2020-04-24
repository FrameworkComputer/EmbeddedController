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

#include "common.h"
#include "ia_structs.h"
#include "ish_aon_share.h"
#include "ish_aon_defs.h"
#include "ish_dma.h"
#include "power_mgt.h"

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
#ifdef CONFIG_ISH_NEW_PM
#define AON_IDT_ENTRY_VEC_LAST		ISH_PMU_WAKEUP_VEC
#else
#define AON_IDT_ENTRY_VEC_FIRST		ISH_PMU_WAKEUP_VEC
#endif

#ifdef CONFIG_ISH_PM_RESET_PREP
/**
 * assume reset prep interrupt vector is greater than PMU wakeup interrupt
 * vector, and also need handle reset prep interrupt
 * (if CONFIG_ISH_PM_RESET_PREP defined)
 */
#ifdef CONFIG_ISH_NEW_PM
#define AON_IDT_ENTRY_VEC_FIRST		ISH_RESET_PREP_VEC
#else
#define AON_IDT_ENTRY_VEC_LAST		ISH_RESET_PREP_VEC
#endif
#else
/* only need handle single PMU wakeup interrupt */
#ifdef CONFIG_ISH_NEW_PM
#define AON_IDT_ENTRY_VEC_FIRST		ISH_PMU_WAKEUP_VEC
#else
#define AON_IDT_ENTRY_VEC_LAST		ISH_PMU_WAKEUP_VEC
#endif
#endif

static void handle_reset(enum ish_pm_state pm_state);

/* ISR for PMU wakeup interrupt */
static void pmu_wakeup_isr(void)
{
	/**
	 * Indicate completion of servicing the interrupt to IOAPIC first
	 * then indicate completion of servicing the interrupt to LAPIC
	 */
	IOAPIC_EOI_REG = ISH_PMU_WAKEUP_VEC;
	LAPIC_EOI_REG = 0x0;

	__asm__ volatile ("iret;");

	__builtin_unreachable();
}

/* ISR for reset prep interrupt */
static void reset_prep_isr(void)
{
	/* mask reset prep avail interrupt */
	PMU_RST_PREP = PMU_RST_PREP_INT_MASK;

	/**
	 * Indicate completion of servicing the interrupt to IOAPIC first
	 * then indicate completion of servicing the interrupt to LAPIC
	 */
	IOAPIC_EOI_REG = ISH_RESET_PREP_VEC;
	LAPIC_EOI_REG = 0x0;

	handle_reset(ISH_PM_STATE_RESET_PREP);

	__builtin_unreachable();
}

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

/**
 * 8 bytes reserved on stack, just for GDB to show the correct stack
 * information when doing source code level debugging
 */
#define AON_SP_RESERVED (8)

/* TSS segment for aon task */
struct tss_entry aon_tss = {
	.prev_task_link = 0,
	.reserved1 = 0,
	.esp0 = (uint8_t *)(CONFIG_AON_PERSISTENT_BASE - AON_SP_RESERVED),
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
	/* task execute entry point */
	.eip = (uint32_t)&ish_aon_main,
	.eflags = 0,
	.eax = 0,
	.ecx = 0,
	.edx = 0,
	.ebx = 0,
	/* set stack top pointer at the end of usable aon memory */
	.esp = CONFIG_AON_PERSISTENT_BASE - AON_SP_RESERVED,
	.ebp = CONFIG_AON_PERSISTENT_BASE - AON_SP_RESERVED,
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

/* snowball structure */
#if defined(CHIP_FAMILY_ISH3)
/* on ISH3, reused ISH2PMC IPC message registers */
#define SNOWBALL_BASE	IPC_ISH2PMC_MSG_BASE
#else
/* from ISH4, used reserved rom part of AON memory */
#define SNOWBALL_BASE	(CONFIG_AON_PERSISTENT_BASE + 256)
#endif

struct snowball_struct *snowball = (void *)SNOWBALL_BASE;


/* In IMR DDR, ISH FW image has a manifest header */
#define ISH_FW_IMAGE_MANIFEST_HEADER_SIZE (0x1000)

/* simple count based delay */
static inline void delay(uint32_t count)
{
	while (count)
		count--;
}

static inline void enable_dma_bcg(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		CCU_BCG_DMA = 1;
	else
		CCU_BCG_EN = CCU_BCG_EN | CCU_BCG_BIT_DMA;
}

static inline void disable_dma_bcg(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		CCU_BCG_DMA = 0;
	else
		CCU_BCG_EN = CCU_BCG_EN & ~CCU_BCG_BIT_DMA;
}

static int store_main_fw(void)
{
	int ret;
	uint64_t imr_fw_addr;
	uint64_t imr_fw_rw_addr;

	imr_fw_addr = (((uint64_t)snowball->uma_base_hi << 32) +
		       snowball->uma_base_lo +
		       snowball->fw_offset +
		       ISH_FW_IMAGE_MANIFEST_HEADER_SIZE);

	imr_fw_rw_addr = (imr_fw_addr
			  + aon_share.main_fw_rw_addr
			  - CONFIG_RAM_BASE);

	/* disable BCG (Block Clock Gating) for DMA, DMA can be accessed now */
	disable_dma_bcg();

	/* store main FW's read and write data region to IMR/UMA DDR */
	ret = ish_dma_copy(
		PAGING_CHAN,
		imr_fw_rw_addr,
		aon_share.main_fw_rw_addr,
		aon_share.main_fw_rw_size,
		SRAM_TO_UMA);

	/* enable BCG for DMA, DMA can't be accessed now */
	enable_dma_bcg();

	if (ret != DMA_RC_OK) {

		aon_share.last_error = AON_ERROR_DMA_FAILED;
		aon_share.error_count++;

		return AON_ERROR_DMA_FAILED;
	}

	return AON_SUCCESS;
}

static int restore_main_fw(void)
{
	int ret;
	uint64_t imr_fw_addr;
	uint64_t imr_fw_ro_addr;
	uint64_t imr_fw_rw_addr;

	imr_fw_addr = (((uint64_t)snowball->uma_base_hi << 32) +
		       snowball->uma_base_lo +
		       snowball->fw_offset +
		       ISH_FW_IMAGE_MANIFEST_HEADER_SIZE);

	imr_fw_ro_addr = (imr_fw_addr
			  + aon_share.main_fw_ro_addr
			  - CONFIG_RAM_BASE);

	imr_fw_rw_addr = (imr_fw_addr
			  + aon_share.main_fw_rw_addr
			  - CONFIG_RAM_BASE);

	/* disable BCG (Block Clock Gating) for DMA, DMA can be accessed now */
	disable_dma_bcg();

	/* restore main FW's read only code and data region from IMR/UMA DDR */
	ret = ish_dma_copy(
		PAGING_CHAN,
		aon_share.main_fw_ro_addr,
		imr_fw_ro_addr,
		aon_share.main_fw_ro_size,
		UMA_TO_SRAM);

	if (ret != DMA_RC_OK) {

		aon_share.last_error = AON_ERROR_DMA_FAILED;
		aon_share.error_count++;

		/* enable BCG for DMA, DMA can't be accessed now */
		enable_dma_bcg();

		return AON_ERROR_DMA_FAILED;
	}

	/* restore main FW's read and write data region from IMR/UMA DDR */
	ret = ish_dma_copy(
			PAGING_CHAN,
			aon_share.main_fw_rw_addr,
			imr_fw_rw_addr,
			aon_share.main_fw_rw_size,
			UMA_TO_SRAM
			);

	/* enable BCG for DMA, DMA can't be accessed now */
	enable_dma_bcg();

	if (ret != DMA_RC_OK) {

		aon_share.last_error = AON_ERROR_DMA_FAILED;
		aon_share.error_count++;

		return AON_ERROR_DMA_FAILED;
	}

	return AON_SUCCESS;
}

#if defined(CHIP_FAMILY_ISH3)
/* on ISH3, the last SRAM bank is reserved for AON use */
#define SRAM_POWER_OFF_BANKS	(CONFIG_RAM_BANKS - 1)
#elif defined(CHIP_FAMILY_ISH4) || defined(CHIP_FAMILY_ISH5)
/* ISH4 and ISH5 have separate AON memory, can power off entire main SRAM */
#define SRAM_POWER_OFF_BANKS	CONFIG_RAM_BANKS
#else
#error "CHIP_FAMILY_ISH(3|4|5) must be defined"
#endif

/**
 * check SRAM bank i power gated status in PMU_SRAM_PG_EN register
 * 1: power gated 0: not power gated
 */
#define BANK_PG_STATUS(i)	(PMU_SRAM_PG_EN & (0x1 << (i)))

/* enable power gate of a SRAM bank */
#define BANK_PG_ENABLE(i)	(PMU_SRAM_PG_EN |= (0x1 << (i)))

/* disable power gate of a SRAM bank */
#define BANK_PG_DISABLE(i)	(PMU_SRAM_PG_EN &= ~(0x1 << (i)))

/**
 * check SRAM bank i disabled status in ISH_SRAM_CTRL_CSFGR register
 * 1: disabled 0: enabled
 */
#define BANK_DISABLE_STATUS(i)	(ISH_SRAM_CTRL_CSFGR & (0x1 << ((i) + 4)))

/* enable a SRAM bank in ISH_SRAM_CTRL_CSFGR register */
#define BANK_ENABLE(i)		(ISH_SRAM_CTRL_CSFGR &= ~(0x1 << ((i) + 4)))

/* disable a SRAM bank in ISH_SRAM_CTRL_CSFGR register */
#define BANK_DISABLE(i)		(ISH_SRAM_CTRL_CSFGR |= (0x1 << ((i) + 4)))

/* SRAM needs time to warm up after power on */
#define SRAM_WARM_UP_DELAY_CNT		10

/* SRAM needs time to enter retention mode */
#define CYCLES_PER_US                  100
#define SRAM_RETENTION_US_DELAY	       5
#define SRAM_RETENTION_CYCLES_DELAY    (SRAM_RETENTION_US_DELAY * CYCLES_PER_US)

static void sram_power(int on)
{
	int i;
	uint32_t bank_size;
	uint32_t sram_addr;
	uint32_t erase_cfg;

	bank_size = CONFIG_RAM_BANK_SIZE;
	sram_addr = CONFIG_RAM_BASE;

	/**
	 * set erase size as one bank, erase control register using DWORD as
	 * size unit, and using 0 based length, i.e if set 0, will erase one
	 * DWORD
	 */
	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		erase_cfg = ((bank_size >> 3) << 2) | 0x1;
	else
		erase_cfg = (((bank_size - 4) >> 2) << 2) | 0x1;

	for (i = 0; i < SRAM_POWER_OFF_BANKS; i++) {

		if (on && (BANK_PG_STATUS(i) || (!IS_ENABLED(CONFIG_ISH_NEW_PM)
						&& BANK_DISABLE_STATUS(i)))) {

			/* power on and enable a bank */
			BANK_PG_DISABLE(i);

			delay(SRAM_WARM_UP_DELAY_CNT);

			if (!IS_ENABLED(CONFIG_ISH_NEW_PM))
				BANK_ENABLE(i);

			/* erase a bank */
			ISH_SRAM_CTRL_ERASE_ADDR = sram_addr + (i * bank_size);
			ISH_SRAM_CTRL_ERASE_CTRL = erase_cfg;

			/* wait erase complete */
			while (ISH_SRAM_CTRL_ERASE_CTRL & 0x1)
				continue;

		} else {
			/* disable and power off a bank */
			if (!IS_ENABLED(CONFIG_ISH_NEW_PM))
				BANK_DISABLE(i);

			BANK_PG_ENABLE(i);
		}

		/**
		 * clear interrupt status register, not allow generate SRAM
		 * interrupts. Bringup already masked all SRAM interrupts when
		 * booting ISH
		 */
		ISH_SRAM_CTRL_INTR = 0xFFFFFFFF;

	}
}

#define RTC_TICKS_IN_SECOND 32768

static  __maybe_unused uint64_t get_rtc(void)
{
	uint32_t lower;
	uint32_t upper;
	do {
		upper = MISC_ISH_RTC_COUNTER1;
		lower = MISC_ISH_RTC_COUNTER0;
	} while (upper != MISC_ISH_RTC_COUNTER1);

	return ((uint64_t)upper << 32) | lower;
}

#ifdef CONFIG_ISH_IPAPG
static int is_ipapg_allowed(void)
{
	uint32_t power_ctrl_enabled, sw_power_req, power_ctrl_wake;
	int system_power_state;

	if (!IS_ENABLED(CONFIG_ISH_IPAPG))
		return 0;

	system_power_state = ((PMU_PMC_HOST_RST_CTL & PMU_HOST_RST_B) == 0);

	PMU_PMC_HOST_RST_CTL = PMU_PMC_HOST_RST_CTL;

	power_ctrl_enabled = PMU_D3_STATUS;
	sw_power_req = PMU_SW_PG_REQ;
	power_ctrl_wake = PMU_PMC_PG_WAKE;

	if (system_power_state)
		power_ctrl_enabled |= PMU_PCE_PG_ALLOWED;

	PMU_INTERNAL_PCE = (power_ctrl_enabled & PMU_PCE_SHADOW_MASK) |
			   (PMU_PCE_CHANGE_DETECTED) | (PMU_PCE_CHANGE_MASK);

	PMU_SW_PG_REQ = sw_power_req | PMU_SW_PG_REQ_B_RISE |
			PMU_SW_PG_REQ_B_FALL;
	PMU_PMC_PG_WAKE = power_ctrl_wake | PMU_PMC_PG_WAKE_RISE |
			  PMU_PMC_PG_WAKE_FALL;
	PMU_D3_STATUS = (PMU_D3_STATUS) & (PMU_D0I3_ENABLE_MASK |
					   PMU_D3_BIT_SET | PMU_BME_BIT_SET);

	power_ctrl_enabled = PMU_D3_STATUS;
	sw_power_req = PMU_SW_PG_REQ;
	power_ctrl_wake = PMU_PMC_PG_WAKE;

	if (system_power_state) {
		uint64_t rtc_start = get_rtc();
		uint64_t rtc_end;
		while (power_ctrl_wake & PMU_PMC_PG_WAKE_VAL) {
			power_ctrl_wake = PMU_PMC_PG_WAKE;
			rtc_end = get_rtc();
			if (rtc_end - rtc_start > RTC_TICKS_IN_SECOND)
				break;
		}
	}

	if (((power_ctrl_enabled & PMU_PCE_PG_ALLOWED) || system_power_state) &&
	    (((sw_power_req & PMU_SW_PG_REQ_B_VAL) == 0) ||
	     ((power_ctrl_enabled & PMU_PCE_PMCRE) == 0)) &&
	    ((power_ctrl_wake & PMU_PMC_PG_WAKE_VAL) == 0))
		return 1;
	else
		return 0;
}
#else
static int is_ipapg_allowed(void)
{
	return 0;
}
#endif

#define NUMBER_IRQ_PINS 30
static uint32_t ioapic_rte[NUMBER_IRQ_PINS];

static int do_ipapg(void)
{
	int ret;
	uint32_t rte_offset = IOAPIC_IOREDTBL;

	for (int pin = 0; pin < ARRAY_SIZE(ioapic_rte); pin++) {
		IOAPIC_IDX = rte_offset + pin * 2;
		ioapic_rte[pin] = IOAPIC_WDW;
	}

	ret = ipapg();

	rte_offset = IOAPIC_IOREDTBL;
	for (int pin = 0; pin < ARRAY_SIZE(ioapic_rte); pin++) {
		IOAPIC_IDX = rte_offset + pin * 2;
		IOAPIC_WDW = ioapic_rte[pin];
	}

	return ret;
}

static inline void set_vnnred_aoncg(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		PMU_VNNAON_RED = 1;
		CCU_AONCG_EN = 1;
	}
}

static inline void clear_vnnred_aoncg(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		PMU_VNNAON_RED = 0;
		CCU_AONCG_EN = 0;
	}
}

static void handle_d0i2(void)
{
	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		pg_exit_save_ctx();
		aon_share.pg_exit = 0;
	}

	/* set main SRAM into retention mode*/
	PMU_LDO_CTRL = PMU_LDO_ENABLE_BIT
		| PMU_LDO_RETENTION_BIT;

	/* delay some cycles before halt */
	delay(SRAM_RETENTION_CYCLES_DELAY);

	set_vnnred_aoncg();

	if (IS_ENABLED(CONFIG_ISH_IPAPG) && is_ipapg_allowed()) {
		uint32_t sram_cfg_reg;

		sram_cfg_reg = ISH_SRAM_CTRL_CSFGR;

		aon_share.pg_exit = do_ipapg();

		if (aon_share.pg_exit)
			ISH_SRAM_CTRL_CSFGR = sram_cfg_reg;
	} else {
		ish_mia_halt();
	}

	/* wakeup from PMU interrupt */

	clear_vnnred_aoncg();

	/* set main SRAM intto normal mode */
	PMU_LDO_CTRL = PMU_LDO_ENABLE_BIT;

	/**
	 * poll LDO_READY status to make sure SRAM LDO is on
	 * (exited retention mode)
	 */
	while (!(PMU_LDO_CTRL & PMU_LDO_READY_BIT))
		continue;

	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		if (aon_share.pg_exit)
			ish_dma_set_msb(PAGING_CHAN, aon_share.uma_msb,
					aon_share.uma_msb);
	}
}

static void handle_d0i3(void)
{
	int ret;

	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		pg_exit_save_ctx();
		aon_share.pg_exit = 0;
	}

	/* store main FW 's context to IMR DDR from main SRAM */
	ret = store_main_fw();

	/* if store main FW failed, then switch back to main FW */
	if (ret != AON_SUCCESS)
		return;

	/* power off main SRAM */
	sram_power(0);

	set_vnnred_aoncg();

	if (IS_ENABLED(CONFIG_ISH_IPAPG) && is_ipapg_allowed()) {
		uint32_t sram_cfg_reg;

		sram_cfg_reg = ISH_SRAM_CTRL_CSFGR;

		aon_share.pg_exit = do_ipapg();

		if (aon_share.pg_exit)
			ISH_SRAM_CTRL_CSFGR = sram_cfg_reg;
	} else {
		ish_mia_halt();
	}

	/* wakeup from PMU interrupt */

	clear_vnnred_aoncg();

	/* power on main SRAM */
	sram_power(1);

	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		if (aon_share.pg_exit)
			ish_dma_set_msb(PAGING_CHAN, aon_share.uma_msb,
					aon_share.uma_msb);
	}

	/* restore main FW 's context to main SRAM from IMR DDR */
	ret = restore_main_fw();

	if (ret != AON_SUCCESS) {
		/* we can't switch back to main FW now, reset ISH */
		handle_reset(ISH_PM_STATE_RESET);
	}
}

static void handle_d3(void)
{
	/* handle D3 */
	handle_reset(ISH_PM_STATE_RESET);
}

static inline void disable_csme_csrirq(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		REG32(IPC_PIMR_CIM_SEC) = 1;
	else
		REG32(IPC_PIMR) &= ~IPC_PIMR_CSME_CSR_BIT;
}

static void handle_reset(enum ish_pm_state pm_state)
{
	/* disable watch dog */
	WDT_CONTROL &= ~WDT_CONTROL_ENABLE_BIT;

	/* disable all gpio interrupts */
	ISH_GPIO_GRER = 0;
	ISH_GPIO_GFER = 0;
	ISH_GPIO_GIMR = 0;

	/* disable CSME CSR irq */
	disable_csme_csrirq();

	/* power off main SRAM */
	sram_power(0);

	while (1) {
		/**
		 * check if host ish driver already set the DMA enable flag
		 *
		 * ISH FW and ISH ipc host driver using IPC_ISH_RMP2 register
		 * for synchronization during ISH boot.
		 * ISH ipc host driver will set DMA_ENABLED_MASK bit when it
		 * is loaded and starts, and clear this bit when it is removed.
		 *
		 * see: https://github.com/torvalds/linux/blob/master/drivers/
		 *      hid/intel-ish-hid/ipc/ipc.c
		 *
		 * we have two kinds of reset situations need to handle here:
		 * 1: reset ISH via uart console cmd or ectool host cmd
		 * 2: S0 -> Sx (reset_prep interrupt)
		 *
		 * for #1, ISH ipc host driver no changed states,
		 * DMA_ENABLED_MASK bit always set, so, will reset ISH directly
		 *
		 * for #2, ISH ipc host driver changed states, and cleared
		 * DMA_ENABLED_MASK bit, then ISH FW received reset_prep
		 * interrupt, ISH will stay in this while loop (most time in
		 * halt state), waiting for DMA_ENABLED_MASK bit was set and
		 * reset ISH then. Since ISH ROM have no power managment, stay
		 * in aontask can save more power especially if system stay in
		 * Sx for long time.
		 *
		 */
		if (IPC_ISH_RMP2 & DMA_ENABLED_MASK) {

			/* clear ISH2HOST doorbell register */
			*IPC_ISH2HOST_DOORBELL_ADDR = 0;

			/* clear error register in MISC space */
			MISC_ISH_ECC_ERR_SRESP = 1;

			/*
			 * Disable power gating of RF(Cache) and ROMs.
			 *
			 * Before switch to aon task, RF and ROMs are already
			 * power gated, so we need disable the power gating
			 * before reset to ROM, to make sure ROM code runs
			 * correctly.
			 */
			PMU_RF_ROM_PWR_CTRL = 0;

			/* reset ISH minute-ia cpu core, will goto ISH ROM */
			ish_mia_reset();

			__builtin_unreachable();
		}

		ish_mia_halt();
	}

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
	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		aon_idt[AON_IDT_ENTRY_VEC_LAST -
			AON_IDT_ENTRY_VEC_FIRST].dword_lo =
			GEN_IDT_DESC_LO(&pmu_wakeup_isr, 0x4, IDT_DESC_FLAGS);

		aon_idt[AON_IDT_ENTRY_VEC_LAST -
			AON_IDT_ENTRY_VEC_FIRST].dword_up =
			GEN_IDT_DESC_UP(&pmu_wakeup_isr, 0x4, IDT_DESC_FLAGS);
	} else {
		aon_idt[0].dword_lo = GEN_IDT_DESC_LO(&pmu_wakeup_isr, 0x4,
			IDT_DESC_FLAGS);

		aon_idt[0].dword_up = GEN_IDT_DESC_UP(&pmu_wakeup_isr, 0x4,
			IDT_DESC_FLAGS);
	}

	if (IS_ENABLED(CONFIG_ISH_PM_RESET_PREP)) {
		/*
		 * set reset prep interrupt gate using LDT code segment
		 * selector(0x4)
		 */
		if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
			aon_idt[0].dword_lo = GEN_IDT_DESC_LO(&reset_prep_isr,
				0x4, IDT_DESC_FLAGS);

			aon_idt[0].dword_up = GEN_IDT_DESC_UP(&reset_prep_isr,
				0x4, IDT_DESC_FLAGS);
		} else {
			aon_idt[AON_IDT_ENTRY_VEC_LAST -
				AON_IDT_ENTRY_VEC_FIRST].dword_lo =
				GEN_IDT_DESC_LO(&reset_prep_isr, 0x4,
				IDT_DESC_FLAGS);

			aon_idt[AON_IDT_ENTRY_VEC_LAST -
				AON_IDT_ENTRY_VEC_FIRST].dword_up =
				GEN_IDT_DESC_UP(&reset_prep_isr, 0x4,
				IDT_DESC_FLAGS);
		}
	}

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
		case ISH_PM_STATE_RESET:
		case ISH_PM_STATE_RESET_PREP:
			handle_reset(aon_share.pm_state);
			break;
		default:
			handle_unknown_state();
			break;
		}

		/* check if D3 rising status */
		if (PMU_D3_STATUS &
		    (PMU_D3_BIT_RISING_EDGE_STATUS | PMU_D3_BIT_SET)) {
			aon_share.pm_state = ISH_PM_STATE_D3;
			handle_d3();
		}

		/* restore main FW's IDT and switch back to main FW */
		__asm__ volatile(
				"lidtl %0;\n"
				:
				: "m" (aon_share.main_fw_idt_hdr)
				);

		if (IS_ENABLED(CONFIG_ISH_IPAPG) && aon_share.pg_exit) {
			mainfw_gdt.entries[tr / sizeof(struct gdt_entry)]
				.flags &= 0xfd;
			pg_exit_restore_ctx();
		}

		__asm__ volatile ("iret;");
	}
}
