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

/* power management internal context data structure */
struct pm_context {
	/* aontask image valid flag */
	int aon_valid;
	/* point to the aon shared data in aontask */
	struct ish_aon_share *aon_share;
	/* TSS segment selector for task switching */
	int aon_tss_selector[2];
} __packed;

static struct pm_context pm_ctx = {
	.aon_valid = 0,
	/* aon shared data located in the start of aon memory */
	.aon_share = (struct ish_aon_share *)CONFIG_ISH_AON_SRAM_BASE_START
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

#endif

static void enter_d0i0(void)
{
	timestamp_t t0, t1;

	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I0;

	/* halt ISH cpu, will wakeup from any interrupt */
	ish_halt();

	t1 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i0_time_us += t1.val - t0.val;
	pm_stats.d0i0_cnt++;
}

#ifdef CONFIG_ISH_PM_D0I1

static void enter_d0i1(void)
{
	timestamp_t t0, t1;

	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I1;

	/* TODO: enable Trunk Clock Gating (TCG) of ISH */

	/* halt ISH cpu, will wakeup from PMU wakeup interrupt */
	ish_halt();

	/* TODO disable Trunk Clock Gating (TCG) of ISH */

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	t1 = get_time();
	pm_stats.d0i1_time_us += t1.val - t0.val;
	pm_stats.d0i1_cnt++;
}

#endif

#ifdef CONFIG_ISH_PM_D0I2

static void enter_d0i2(void)
{
	timestamp_t t0, t1;

	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I2;

	/* TODO: enable Trunk Clock Gating (TCG) of ISH */

	switch_to_aontask();
	/* returned from aontask */

	/* TODO just for test, will remove later */
	ish_halt();

	/* TODO disable Trunk Clock Gating (TCG) of ISH */

	t1 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i2_time_us += t1.val - t0.val;
	pm_stats.d0i2_cnt++;
}

#endif

#ifdef CONFIG_ISH_PM_D0I3

static void enter_d0i3(void)
{
	timestamp_t t0, t1;

	t0 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I3;

	/*TODO some preparing work for D0i3 */

	switch_to_aontask();

	/*TODO just for test, will remove later */
	ish_halt();

	/*TODO some restore work for D0i3 */

	t1 = get_time();

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;

	pm_stats.d0i3_time_us += t1.val - t0.val;
	pm_stats.d0i3_cnt++;
}

#endif

static int d0ix_decide(uint32_t idle_us)
{
	int pm_state = ISH_PM_STATE_D0I0;

	if (DEEP_SLEEP_ALLOWED) {
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

static void pm_process(uint32_t idle_us)
{
	int decide;

	decide = d0ix_decide(idle_us);

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

#ifdef CONFIG_ISH_PM_AONTASK
	init_aon_task();
#endif

	/* unmask all wake up events */
	PMU_MASK_EVENT = ~PMU_MASK_EVENT_BIT_ALL;
}

void __idle(void)
{
	timestamp_t t0;
	int next_delay = 0;

	while (1) {
		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		pm_process(next_delay);
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
