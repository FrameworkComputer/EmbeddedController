/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <console.h>
#include <task.h>
#include <system.h>
#include <hwtimer.h>
#include <util.h>
#include "interrupts.h"
#include "aontaskfw/ish_aon_share.h"
#include "power_mgt.h"
#include "watchdog.h"
#include "ish_dma.h"

#ifdef CONFIG_ISH_PM_DEBUG
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#else
#define CPUTS(outstr)
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

#ifdef CONFIG_WATCHDOG
extern void watchdog_enable(void);
extern void watchdog_disable(void);
#endif

/* defined in link script: core/minute-ia/ec.lds.S */
extern uint32_t __aon_ro_start;
extern uint32_t __aon_ro_end;
extern uint32_t __aon_rw_start;
extern uint32_t __aon_rw_end;

/**
 * on ISH, uart interrupt can only wakeup ISH from low power state via
 * CTS pin, but most ISH platforms only have Rx and Tx pins, no CTS pin
 * exposed, so, we need block ISH enter low power state for a while when
 * console is in use.
 * fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the low speed clock is not used.
 */
#define CONSOLE_IN_USE_ON_BOOT_TIME (15*SECOND)

/* power management internal context data structure */
struct pm_context {
	/* aontask image valid flag */
	int aon_valid;
	/* point to the aon shared data in aontask */
	struct ish_aon_share *aon_share;
	/* TSS segment selector for task switching */
	int aon_tss_selector[2];
	/* console expire time */
	timestamp_t console_expire_time;
	/* console in use timeout */
	int console_in_use_timeout_sec;
} __packed;

static struct pm_context pm_ctx = {
	.aon_valid = 0,
	/* aon shared data located in the start of aon memory */
	.aon_share = (struct ish_aon_share *)CONFIG_ISH_AON_SRAM_BASE_START,
	.console_in_use_timeout_sec = 60
};

/* D0ix statistics data, including each state's count and total stay time */
struct pm_statistics {
	uint64_t d0i0_cnt;
	uint64_t d0i0_time_us;

#ifdef CONFIG_ISH_PM_D0I1
	uint64_t d0i1_cnt;
	uint64_t d0i1_time_us;
#endif

#ifdef CONFIG_ISH_PM_D0I2
	uint64_t d0i2_cnt;
	uint64_t d0i2_time_us;
#endif

#ifdef CONFIG_ISH_PM_D0I3
	uint64_t d0i3_cnt;
	uint64_t d0i3_time_us;
#endif

} __packed;

static struct pm_statistics pm_stats;

#ifdef CONFIG_ISH_PM_AONTASK

/* The GDT which initialized in init.S */
extern struct gdt_entry __gdt[];
extern struct gdt_header __gdt_ptr[];

/* TSS desccriptor for saving main FW's cpu context during aontask switching */
static struct tss_entry main_tss;

/**
 * add new entry in GDT
 * if defined 'CONFIG_ISH_PM_AONTASK', the GDT which defined in init.S will
 * have 3 more empty placeholder entries, this function is help to update
 * these entries which needed by x86's HW task switching method
 *
 * @param desc_lo	lower DWORD of the entry descriptor
 * @param desc_up	upper DWORD of the entry descriptor
 *
 * @return		the descriptor selector index of the added entry
 */
static uint32_t add_gdt_entry(uint32_t desc_lo, uint32_t desc_up)
{
	int index;

	/**
	 * get the first empty entry of GDT which defined in init.S
	 * each entry has a fixed size of 8 bytes
	 */
	index = __gdt_ptr[0].limit >> 3;

	/* add the new entry descriptor to the GDT */
	__gdt[index].dword_lo = desc_lo;
	__gdt[index].dword_up = desc_up;

	/* update GDT's limit size */
	__gdt_ptr[0].limit += sizeof(struct gdt_entry);

	return __gdt_ptr[0].limit - sizeof(struct gdt_entry);
}

static void init_aon_task(void)
{
	uint32_t desc_lo, desc_up;
	struct ish_aon_share *aon_share = pm_ctx.aon_share;
	struct tss_entry *aon_tss = aon_share->aon_tss;

	if (aon_share->magic_id != AON_MAGIC_ID) {
		pm_ctx.aon_valid = 0;
		return;
	}

	pm_ctx.aon_valid = 1;

	pm_ctx.aon_tss_selector[0] = 0;

	/* fill in the 3 placeholder GDT entries */

	/* TSS's limit specified as 0x67, to allow the task has permission to
	 * access I/O port using IN/OUT instructions,'iomap_base_addr' field
	 * must be greater than or equal to TSS' limit
	 * see 'I/O port permissions' on
	 *	https://en.wikipedia.org/wiki/Task_state_segment
	 */
	main_tss.iomap_base_addr = GDT_DESC_TSS_LIMIT;

	/* set GDT entry 3 for TSS descriptor of main FW
	 * limit: 0x67
	 * Present = 1, DPL = 0
	 */
	desc_lo = GEN_GDT_DESC_LO((uint32_t)&main_tss,
			GDT_DESC_TSS_LIMIT, GDT_DESC_TSS_FLAGS);
	desc_up = GEN_GDT_DESC_UP((uint32_t)&main_tss,
			GDT_DESC_TSS_LIMIT, GDT_DESC_TSS_FLAGS);
	add_gdt_entry(desc_lo, desc_up);

	/* set GDT entry 4 for TSS descriptor of aontask
	 * limit: 0x67
	 * Present = 1, DPL = 0, Accessed = 1
	 */
	desc_lo = GEN_GDT_DESC_LO((uint32_t)aon_tss,
			GDT_DESC_TSS_LIMIT, GDT_DESC_TSS_FLAGS);
	desc_up = GEN_GDT_DESC_UP((uint32_t)aon_tss,
			GDT_DESC_TSS_LIMIT, GDT_DESC_TSS_FLAGS);
	pm_ctx.aon_tss_selector[1] = add_gdt_entry(desc_lo, desc_up);

	/* set GDT entry 5 for LDT descriptor of aontask
	 * Present = 1, DPL = 0, Readable = 1
	 */
	desc_lo = GEN_GDT_DESC_LO((uint32_t)aon_share->aon_ldt,
				  aon_share->aon_ldt_size, GDT_DESC_LDT_FLAGS);
	desc_up = GEN_GDT_DESC_UP((uint32_t)aon_share->aon_ldt,
				  aon_share->aon_ldt_size, GDT_DESC_LDT_FLAGS);
	aon_tss->ldt_seg_selector = add_gdt_entry(desc_lo, desc_up);

	/* update GDT register and set current TSS as main_tss (GDT entry 3) */
	__asm__ volatile("lgdt __gdt_ptr;\n"
			 "push %eax;\n"
			 "movw $0x18, %ax;\n"
			 "ltr %ax;\n"
			 "pop %eax;");

	aon_share->main_fw_ro_addr = (uint32_t)&__aon_ro_start;
	aon_share->main_fw_ro_size = (uint32_t)&__aon_ro_end -
				     (uint32_t)&__aon_ro_start;

	aon_share->main_fw_rw_addr = (uint32_t)&__aon_rw_start;
	aon_share->main_fw_rw_size = (uint32_t)&__aon_rw_end -
				     (uint32_t)&__aon_rw_start;

	ish_dma_init();
}

static inline void check_aon_task_status(void)
{
	struct ish_aon_share *aon_share = pm_ctx.aon_share;

	if (aon_share->last_error != AON_SUCCESS) {
		CPRINTF("aontask has errors:\n");
		CPRINTF("    last error:   %d\n", aon_share->last_error);
		CPRINTF("    error counts: %d\n", aon_share->error_count);
	}
}

static void switch_to_aontask(void)
{
	interrupt_disable();

	__sync_synchronize();

	/* disable cache and flush cache */
	__asm__ volatile("movl %%cr0, %%eax;\n"
			 "orl $0x60000000, %%eax;\n"
			 "movl %%eax, %%cr0;\n"
			 "wbinvd;"
			 :
			 :
			 : "eax");

	/* switch to aontask through a far call with aontask's TSS selector */
	__asm__ volatile("lcall *%0;" ::"m"(*pm_ctx.aon_tss_selector) :);

	/* clear TS (Task Switched) flag and enable cache */
	__asm__ volatile("clts;\n"
			 "movl %%cr0, %%eax;\n"
			 "andl $0x9FFFFFFF, %%eax;\n"
			 "movl %%eax, %%cr0;"
			 :
			 :
			 : "eax");

	interrupt_enable();
}

__attribute__ ((noreturn))
static void handle_reset_in_aontask(int pm_state)
{
	pm_ctx.aon_share->pm_state = pm_state;

	/* only enable PMU wakeup interrupt */
	disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

#ifdef CONFIG_ISH_PM_RESET_PREP
	task_enable_irq(ISH_RESET_PREP_IRQ);
#endif

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* enable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 1;

	switch_to_aontask();

	__builtin_unreachable();
}

#endif

static void enter_d0i0(void)
{
	timestamp_t t0, t1;

	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I0;

	/* halt ISH cpu, will wakeup from any interrupt */
	ish_mia_halt();

	t1 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i0_time_us += t1.val - t0.val;
	pm_stats.d0i0_cnt++;
}

#ifdef CONFIG_ISH_PM_D0I1

static void enter_d0i1(void)
{
	uint64_t current_irq_map;

	timestamp_t t0, t1;
	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I1;

	/* only enable PMU wakeup interrupt */
	current_irq_map = disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

#ifdef CONFIG_ISH_PM_RESET_PREP
	task_enable_irq(ISH_RESET_PREP_IRQ);
#endif

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* halt ISH cpu, will wakeup from PMU wakeup interrupt */
	ish_mia_halt();

	/* disable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 0;

	/* restore interrupts */
	task_disable_irq(ISH_PMU_WAKEUP_IRQ);
	restore_interrupts(current_irq_map);

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	t1 = get_time();
	pm_stats.d0i1_time_us += t1.val - t0.val;
	pm_stats.d0i1_cnt++;
}

#endif

#ifdef CONFIG_ISH_PM_D0I2

static void enter_d0i2(void)
{
	uint64_t current_irq_map;

	timestamp_t t0, t1;
	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I2;

	/* only enable PMU wakeup interrupt */
	current_irq_map = disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

#ifdef CONFIG_ISH_PM_RESET_PREP
	task_enable_irq(ISH_RESET_PREP_IRQ);
#endif

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* enable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 1;

	switch_to_aontask();

	/* returned from aontask */

	/* disable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 0;

	/* disable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 0;

	/* restore interrupts */
	task_disable_irq(ISH_PMU_WAKEUP_IRQ);
	restore_interrupts(current_irq_map);

	t1 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i2_time_us += t1.val - t0.val;
	pm_stats.d0i2_cnt++;
}

#endif

#ifdef CONFIG_ISH_PM_D0I3

static void enter_d0i3(void)
{
	uint64_t current_irq_map;
	timestamp_t t0, t1;

	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I3;

	/* only enable PMU wakeup interrupt */
	current_irq_map = disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

#ifdef CONFIG_ISH_PM_RESET_PREP
	task_enable_irq(ISH_RESET_PREP_IRQ);
#endif

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* enable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 1;

	switch_to_aontask();

	/* returned from aontask */

	/* disable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 0;

	/* disable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 0;

	/* restore interrupts */
	task_disable_irq(ISH_PMU_WAKEUP_IRQ);
	restore_interrupts(current_irq_map);

	t1 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i3_time_us += t1.val - t0.val;
	pm_stats.d0i3_cnt++;
}

#endif

static int d0ix_decide(timestamp_t cur_time, uint32_t idle_us)
{
	int pm_state = ISH_PM_STATE_D0I0;

	if (DEEP_SLEEP_ALLOWED) {

		/* check if the console use has expired. */
		if (sleep_mask & SLEEP_MASK_CONSOLE) {
			if (cur_time.val > pm_ctx.console_expire_time.val) {
				enable_sleep(SLEEP_MASK_CONSOLE);
				ccprints("Disabling console in deep sleep");
			} else {
				return pm_state;
			}
		}

#ifdef CONFIG_ISH_PM_D0I1
		pm_state = ISH_PM_STATE_D0I1;
#endif

#ifdef CONFIG_ISH_PM_D0I2
		if (idle_us >= CONFIG_ISH_D0I2_MIN_USEC && pm_ctx.aon_valid)
			pm_state = ISH_PM_STATE_D0I2;
#endif

#ifdef CONFIG_ISH_PM_D0I3
		if (idle_us >= CONFIG_ISH_D0I3_MIN_USEC && pm_ctx.aon_valid)
			pm_state = ISH_PM_STATE_D0I3;
#endif

	}

	return pm_state;
}

static void pm_process(timestamp_t cur_time, uint32_t idle_us)
{
	int decide;

	decide = d0ix_decide(cur_time, idle_us);

#ifdef CONFIG_WATCHDOG
	watchdog_disable();
#endif

	switch (decide) {
#ifdef CONFIG_ISH_PM_D0I1
	case ISH_PM_STATE_D0I1:
		enter_d0i1();
		break;
#endif
#ifdef CONFIG_ISH_PM_D0I2
	case ISH_PM_STATE_D0I2:
		enter_d0i2();
		break;
#endif
#ifdef CONFIG_ISH_PM_D0I3
	case ISH_PM_STATE_D0I3:
		enter_d0i3();
		break;
#endif
	default:
		enter_d0i0();
		break;
	}

#if defined(CONFIG_ISH_PM_D0I2) || defined(CONFIG_ISH_PM_D0I3)
	if (decide == ISH_PM_STATE_D0I2 || decide == ISH_PM_STATE_D0I3)
		check_aon_task_status();
#endif

#ifdef CONFIG_WATCHDOG
	watchdog_enable();
	watchdog_reload();
#endif

}

void ish_pm_init(void)
{
	/* clear reset bit */
	ISH_RST_REG = 0;

	/* clear reset history register in CCU */
	CCU_RST_HST = CCU_RST_HST;

	/* disable TCG and disable BCG */
	CCU_TCG_EN = 0;
	CCU_BCG_EN = 0;

#ifdef CONFIG_ISH_PM_AONTASK
	init_aon_task();
#endif

	/* unmask all wake up events */
	PMU_MASK_EVENT = ~PMU_MASK_EVENT_BIT_ALL;

#ifdef CONFIG_ISH_PM_RESET_PREP
	/* unmask reset prep avail interrupt */
	PMU_RST_PREP = 0;

	task_enable_irq(ISH_RESET_PREP_IRQ);
#endif

#ifdef CONFIG_ISH_PM_D3

	/* unmask D3 and BME interrupts */
	PMU_D3_STATUS &= (PMU_D3_BIT_SET | PMU_BME_BIT_SET);

	if ((!(PMU_D3_STATUS & PMU_D3_BIT_SET)) &&
			(PMU_D3_STATUS & PMU_BME_BIT_SET)) {
		PMU_D3_STATUS = PMU_D3_STATUS;
	}

	task_enable_irq(ISH_D3_RISE_IRQ);
	task_enable_irq(ISH_D3_FALL_IRQ);
	task_enable_irq(ISH_BME_RISE_IRQ);
	task_enable_irq(ISH_BME_FALL_IRQ);

#endif

}

__attribute__ ((noreturn))
void ish_pm_reset(void)
{

#ifdef CONFIG_ISH_PM_AONTASK
	if (pm_ctx.aon_valid) {
		handle_reset_in_aontask(ISH_PM_STATE_RESET_PREP);
	} else {
		ish_mia_reset();
	}
#else
	ish_mia_reset();
#endif

	__builtin_unreachable();
}

void __idle(void)
{
	timestamp_t t0;
	int next_delay = 0;

	/**
	 * initialize console in use to true and specify the console expire
	 * time in order to give a fixed window on boot
	 */
	disable_sleep(SLEEP_MASK_CONSOLE);
	pm_ctx.console_expire_time.val = get_time().val +
					 CONSOLE_IN_USE_ON_BOOT_TIME;

	while (1) {
		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		pm_process(t0, next_delay);
	}
}

/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, char **argv)
{
#if defined(CONFIG_ISH_PM_D0I2) || defined(CONFIG_ISH_PM_D0I3)
	struct ish_aon_share *aon_share = pm_ctx.aon_share;
#endif

	ccprintf("Aontask exist: %s\n", pm_ctx.aon_valid ? "Yes" : "No");
	ccprintf("Idle sleep:\n");
	ccprintf("    D0i0:\n");
	ccprintf("        counts: %ld\n", pm_stats.d0i0_cnt);
	ccprintf("        time:   %.6lds\n", pm_stats.d0i0_time_us);

	ccprintf("Deep sleep:\n");
#ifdef CONFIG_ISH_PM_D0I1
	ccprintf("    D0i1:\n");
	ccprintf("        counts: %ld\n", pm_stats.d0i1_cnt);
	ccprintf("        time:   %.6lds\n", pm_stats.d0i1_time_us);
#endif

#ifdef CONFIG_ISH_PM_D0I2
	if (pm_ctx.aon_valid) {
		ccprintf("    D0i2:\n");
		ccprintf("        counts: %ld\n", pm_stats.d0i2_cnt);
		ccprintf("        time:   %.6lds\n", pm_stats.d0i2_time_us);
	}
#endif

#ifdef CONFIG_ISH_PM_D0I3
	if (pm_ctx.aon_valid) {
		ccprintf("    D0i3:\n");
		ccprintf("        counts: %ld\n", pm_stats.d0i3_cnt);
		ccprintf("        time:   %.6lds\n", pm_stats.d0i3_time_us);
	}
#endif

#if defined(CONFIG_ISH_PM_D0I2) || defined(CONFIG_ISH_PM_D0I3)
	if (pm_ctx.aon_valid) {
		ccprintf("    Aontask status:\n");
		ccprintf("        last error:   %d\n", aon_share->last_error);
		ccprintf("        error counts: %d\n", aon_share->error_count);
	}
#endif

	ccprintf("Total time on: %.6lds\n", get_time().val);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");


#ifdef CONFIG_ISH_PM_D0I1

/**
 * main FW only need handle PMU wakeup interrupt for D0i1 state, aontask will
 * handle PMU wakeup interrupt for other low power states
 */
static void pmu_wakeup_isr(void)
{
	/* at current nothing need to do */
}

DECLARE_IRQ(ISH_PMU_WAKEUP_IRQ, pmu_wakeup_isr);

#endif

#ifdef CONFIG_ISH_PM_RESET_PREP

/**
 * from ISH5.0, when system doing S0->Sx transition, will receive reset prep
 * interrupt, will switch to aontask for handling
 *
 */

__attribute__ ((noreturn))
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

	if (pm_ctx.aon_valid) {
		handle_reset_in_aontask(ISH_PM_STATE_RESET_PREP);
	} else {
		ish_mia_reset();
	}

	__builtin_unreachable();
}

DECLARE_IRQ(ISH_RESET_PREP_IRQ, reset_prep_isr);

#endif

#ifdef CONFIG_ISH_PM_D3

static void handle_d3(uint32_t irq_vec)
{
	PMU_D3_STATUS = PMU_D3_STATUS;

	if (PMU_D3_STATUS & (PMU_D3_BIT_RISING_EDGE_STATUS | PMU_D3_BIT_SET)) {

		if (!pm_ctx.aon_valid)
			ish_mia_reset();

		/**
		 * Indicate completion of servicing the interrupt to IOAPIC
		 * first then indicate completion of servicing the interrupt
		 * to LAPIC
		 */
		REG32(IOAPIC_EOI_REG) = irq_vec;
		REG32(LAPIC_EOI_REG) = 0x0;

		pm_ctx.aon_share->pm_state = ISH_PM_STATE_D3;

		/* only enable PMU wakeup interrupt */
		disable_all_interrupts();
		task_enable_irq(ISH_PMU_WAKEUP_IRQ);

#ifdef CONFIG_ISH_PM_RESET_PREP
		task_enable_irq(ISH_RESET_PREP_IRQ);
#endif

		/* enable Trunk Clock Gating (TCG) of ISH */
		CCU_TCG_EN = 1;

		/* enable power gating of RF(Cache) and ROMs */
		PMU_RF_ROM_PWR_CTRL = 1;

		switch_to_aontask();

		__builtin_unreachable();
	}
}

static void d3_rise_isr(void)
{
	handle_d3(ISH_D3_RISE_VEC);
}

static void d3_fall_isr(void)
{
	handle_d3(ISH_D3_FALL_VEC);
}

static void bme_rise_isr(void)
{
	handle_d3(ISH_BME_RISE_VEC);
}

static void bme_fall_isr(void)
{
	handle_d3(ISH_BME_FALL_VEC);
}

DECLARE_IRQ(ISH_D3_RISE_IRQ, d3_rise_isr);
DECLARE_IRQ(ISH_D3_FALL_IRQ, d3_fall_isr);
DECLARE_IRQ(ISH_BME_RISE_IRQ, bme_rise_isr);
DECLARE_IRQ(ISH_BME_FALL_IRQ, bme_fall_isr);

#endif

void ish_pm_refresh_console_in_use(void)
{
	disable_sleep(SLEEP_MASK_CONSOLE);

	/* Set console in use expire time. */
	pm_ctx.console_expire_time = get_time();
	pm_ctx.console_expire_time.val +=
				pm_ctx.console_in_use_timeout_sec * SECOND;
}
