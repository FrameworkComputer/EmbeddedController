/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "aontaskfw/ish_aon_share.h"
#include "common.h"
#include "console.h"
#include "hwtimer.h"
#include "interrupts.h"
#include "ish_dma.h"
#include "ish_persistent_data.h"
#include "power_mgt.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/* defined in link script: core/minute-ia/ec.lds.S */
extern uint32_t __aon_ro_start;
extern uint32_t __aon_ro_end;
extern uint32_t __aon_rw_start;
extern uint32_t __aon_rw_end;

static void pg_exit_restore_hw(void)
{
	lapic_restore();
	i2c_port_restore();

	CCU_RST_HST = CCU_RST_HST;
	CCU_TCG_ENABLE = 0;
	CCU_BCG_ENABLE = 0;

	CCU_BCG_MIA = 0;
	CCU_BCG_DMA = 0;
	CCU_BCG_I2C = 0;
	CCU_BCG_SPI = 0;
	CCU_BCG_UART = 0;
	CCU_BCG_GPIO = 0;
}

/**
 * on ISH, uart interrupt can only wakeup ISH from low power state via
 * CTS pin, but most ISH platforms only have Rx and Tx pins, no CTS pin
 * exposed, so, we need block ISH enter low power state for a while when
 * console is in use.
 * fixed amount of time to keep the console in use flag true after boot in
 * order to give a permanent window in which the low speed clock is not used.
 */
#define CONSOLE_IN_USE_ON_BOOT_TIME (15 * SECOND)

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
	.aon_share = (struct ish_aon_share *)CONFIG_AON_RAM_BASE,
	.console_in_use_timeout_sec = 60
};

/* D0ix statistics data, including each state's count and total stay time */
struct pm_stat {
	uint64_t count;
	uint64_t total_time_us;
};

struct pm_statistics {
	struct pm_stat d0i0;
	struct pm_stat d0i1;
	struct pm_stat d0i2;
	struct pm_stat d0i3;
	struct pm_stat pg;
};

static struct pm_statistics pm_stats;

/*
 * Log a new statistic
 *
 * t0: start time, in us
 * t1: end time, in us
 */
static void log_pm_stat(struct pm_stat *stat, uint32_t t0, uint32_t t1)
{
	stat->total_time_us += t1 - t0;
	stat->count++;
}

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
	desc_lo = GEN_GDT_DESC_LO((uint32_t)&main_tss, GDT_DESC_TSS_LIMIT,
				  GDT_DESC_TSS_FLAGS);
	desc_up = GEN_GDT_DESC_UP((uint32_t)&main_tss, GDT_DESC_TSS_LIMIT,
				  GDT_DESC_TSS_FLAGS);
	add_gdt_entry(desc_lo, desc_up);

	/* set GDT entry 4 for TSS descriptor of aontask
	 * limit: 0x67
	 * Present = 1, DPL = 0, Accessed = 1
	 */
	desc_lo = GEN_GDT_DESC_LO((uint32_t)aon_tss, GDT_DESC_TSS_LIMIT,
				  GDT_DESC_TSS_FLAGS);
	desc_up = GEN_GDT_DESC_UP((uint32_t)aon_tss, GDT_DESC_TSS_LIMIT,
				  GDT_DESC_TSS_FLAGS);
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
	aon_share->main_fw_ro_size =
		(uint32_t)&__aon_ro_end - (uint32_t)&__aon_ro_start;

	aon_share->main_fw_rw_addr = (uint32_t)&__aon_rw_start;
	aon_share->main_fw_rw_size =
		(uint32_t)&__aon_rw_end - (uint32_t)&__aon_rw_start;

	aon_share->uma_msb = IPC_UMA_RANGE_LOWER_1;

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

__noreturn static void handle_reset_in_aontask(enum ish_pm_state pm_state)
{
	pm_ctx.aon_share->pm_state = pm_state;

	/* only enable PMU wakeup interrupt */
	disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

	if (IS_ENABLED(CONFIG_ISH_PM_RESET_PREP))
		task_enable_irq(ISH_RESET_PREP_IRQ);

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
	uint32_t t0, t1;

	t0 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I0;

	/* halt ISH cpu, will wakeup from any interrupt */
	ish_mia_halt();

	t1 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;
	log_pm_stat(&pm_stats.d0i0, t0, t1);
}

/**
 * ISH PMU does not support both-edge interrupt triggered gpio configuration.
 * If both edges are configured, then the ISH can't stay in low poer mode
 * because it will exit immediately.
 *
 * As a workaround, we scan all gpio pins which have been configured as
 * both-edge triggered, and then temporarily set each gpio pin to the single
 * edge trigger that is opposite of its value, then restore the both-edge
 * trigger configuration immediately after exiting low power mode.
 */
static uint32_t convert_both_edge_gpio_to_single_edge(void)
{
	uint32_t both_edge_pins = 0;
	int i = 0;

	/**
	 * scan GPIO GFER, GRER and GIMR registers to find the both edge
	 * interrupt trigger mode enabled pins.
	 */
	for (i = 0; i < 32; i++) {
		if (ISH_GPIO_GIMR & BIT(i) && ISH_GPIO_GRER & BIT(i) &&
		    ISH_GPIO_GFER & BIT(i)) {
			/* Record the pin so we can restore it later */
			both_edge_pins |= BIT(i);

			if (ISH_GPIO_GPLR & BIT(i)) {
				/* pin is high, just keep falling edge mode */
				ISH_GPIO_GRER &= ~BIT(i);
			} else {
				/* pin is low, just keep rising edge mode */
				ISH_GPIO_GFER &= ~BIT(i);
			}
		}
	}

	return both_edge_pins;
}

static void restore_both_edge_gpio_config(uint32_t both_edge_pin_map)
{
	ISH_GPIO_GRER |= both_edge_pin_map;
	ISH_GPIO_GFER |= both_edge_pin_map;
}

static void enter_d0i1(void)
{
	uint64_t current_irq_map;
	uint32_t both_edge_gpio_pins;
	uint32_t t0, t1;

	/* only enable PMU wakeup interrupt */
	current_irq_map = disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

	if (IS_ENABLED(CONFIG_ISH_PM_RESET_PREP))
		task_enable_irq(ISH_RESET_PREP_IRQ);

	t0 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I1;

	both_edge_gpio_pins = convert_both_edge_gpio_to_single_edge();

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* halt ISH cpu, will wakeup from PMU wakeup interrupt */
	ish_mia_halt();

	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		clear_fabric_error();

	/* disable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 0;

	restore_both_edge_gpio_config(both_edge_gpio_pins);

	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;
	t1 = __hw_clock_source_read();
	log_pm_stat(&pm_stats.d0i1, t0, t1);

	/* Reload watchdog before enabling interrupts again */
	watchdog_reload();

	/* restore interrupts */
	task_disable_irq(ISH_PMU_WAKEUP_IRQ);
	restore_interrupts(current_irq_map);
}

static void enter_d0i2(void)
{
	uint64_t current_irq_map;
	uint32_t both_edge_gpio_pins;
	uint32_t t0, t1;

	/* only enable PMU wakeup interrupt */
	current_irq_map = disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

	if (IS_ENABLED(CONFIG_ISH_PM_RESET_PREP))
		task_enable_irq(ISH_RESET_PREP_IRQ);

	t0 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I2;

	both_edge_gpio_pins = convert_both_edge_gpio_to_single_edge();

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* enable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 1;

	switch_to_aontask();

	/* returned from aontask */

	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		if (pm_ctx.aon_share->pg_exit)
			pg_exit_restore_hw();
	}

	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		clear_fabric_error();

	/* disable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 0;

	/* disable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 0;

	restore_both_edge_gpio_config(both_edge_gpio_pins);

	t1 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;
	log_pm_stat(&pm_stats.d0i2, t0, t1);
	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		if (pm_ctx.aon_share->pg_exit)
			log_pm_stat(&pm_stats.pg, t0, t1);
	}

	/* Reload watchdog before enabling interrupts again */
	watchdog_reload();

	/* restore interrupts */
	task_disable_irq(ISH_PMU_WAKEUP_IRQ);
	restore_interrupts(current_irq_map);
}

static void enter_d0i3(void)
{
	uint64_t current_irq_map;
	uint32_t both_edge_gpio_pins;
	uint32_t t0, t1;

	/* only enable PMU wakeup interrupt */
	current_irq_map = disable_all_interrupts();
	task_enable_irq(ISH_PMU_WAKEUP_IRQ);

	if (IS_ENABLED(CONFIG_ISH_PM_RESET_PREP))
		task_enable_irq(ISH_RESET_PREP_IRQ);

	t0 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0I3;

	both_edge_gpio_pins = convert_both_edge_gpio_to_single_edge();

	/* enable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 1;

	/* enable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 1;

	switch_to_aontask();

	/* returned from aontask */

	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		if (pm_ctx.aon_share->pg_exit)
			pg_exit_restore_hw();
	}

	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		clear_fabric_error();

	/* disable power gating of RF(Cache) and ROMs */
	PMU_RF_ROM_PWR_CTRL = 0;

	/* disable Trunk Clock Gating (TCG) of ISH */
	CCU_TCG_EN = 0;

	restore_both_edge_gpio_config(both_edge_gpio_pins);

	t1 = __hw_clock_source_read();
	pm_ctx.aon_share->pm_state = ISH_PM_STATE_D0;
	log_pm_stat(&pm_stats.d0i3, t0, t1);
	if (IS_ENABLED(CONFIG_ISH_IPAPG)) {
		if (pm_ctx.aon_share->pg_exit)
			log_pm_stat(&pm_stats.pg, t0, t1);
	}

	/* Reload watchdog before enabling interrupts again */
	watchdog_reload();

	/* restore interrupts */
	task_disable_irq(ISH_PMU_WAKEUP_IRQ);
	restore_interrupts(current_irq_map);
}

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

		if (IS_ENABLED(CONFIG_ISH_PM_D0I3) &&
		    idle_us >= CONFIG_ISH_D0I3_MIN_USEC && pm_ctx.aon_valid)
			pm_state = ISH_PM_STATE_D0I3;

		else if (IS_ENABLED(CONFIG_ISH_PM_D0I2) &&
			 idle_us >= CONFIG_ISH_D0I2_MIN_USEC &&
			 pm_ctx.aon_valid)
			pm_state = ISH_PM_STATE_D0I2;

		else if (IS_ENABLED(CONFIG_ISH_PM_D0I1))
			pm_state = ISH_PM_STATE_D0I1;
	}

	return pm_state;
}

static void pre_setting_d0ix(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		PMU_VNN_REQ = PMU_VNN_REQ;
		uart_to_idle();
	}
}

static void post_setting_d0ix(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		uart_port_restore();
}

static void pm_process(timestamp_t cur_time, uint32_t idle_us)
{
	int decide;

	decide = d0ix_decide(cur_time, idle_us);

	switch (decide) {
	case ISH_PM_STATE_D0I1:
		pre_setting_d0ix();
		enter_d0i1();
		post_setting_d0ix();
		break;
	case ISH_PM_STATE_D0I2:
		pre_setting_d0ix();
		enter_d0i2();
		post_setting_d0ix();
		check_aon_task_status();
		break;
	case ISH_PM_STATE_D0I3:
		pre_setting_d0ix();
		enter_d0i3();
		post_setting_d0ix();
		check_aon_task_status();
		break;
	default:
		enter_d0i0();
		break;
	}
}

static void reset_bcg(void)
{
	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		CCU_BCG_MIA = 0;
		CCU_BCG_DMA = 0;
		CCU_BCG_I2C = 0;
		CCU_BCG_SPI = 0;
		CCU_BCG_UART = 0;
		CCU_BCG_GPIO = 0;
	} else {
		CCU_BCG_EN = 0;
	}
}

static void enable_d3bme_irqs(void)
{
	task_enable_irq(ISH_D3_RISE_IRQ);
	if (!IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		task_enable_irq(ISH_D3_FALL_IRQ);
		task_enable_irq(ISH_BME_RISE_IRQ);
		task_enable_irq(ISH_BME_FALL_IRQ);
	}
}

void ish_pm_init(void)
{
	/* clear reset bit */
	ISH_RST_REG = 0;

	/* clear reset history register in CCU */
	CCU_RST_HST = CCU_RST_HST;

#if defined(CHIP_VARIANT_ISH5P4)
	if (IS_ENABLED(CONFIG_ISH_NEW_PM))
		PMU_D3_STATUS_1 = 0xffffffff;
#endif

	/* disable TCG and disable BCG */
	CCU_TCG_ENABLE = 0;
	CCU_BCG_ENABLE = 0;

	/* Disable power gate of CACHE and ROM */
	PMU_RF_ROM_PWR_CTRL = 0;

	reset_bcg();

	if (IS_ENABLED(CONFIG_ISH_PM_AONTASK))
		init_aon_task();

	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		PMU_GPIO_WAKE_MASK0 = 0;
		PMU_GPIO_WAKE_MASK1 = 0;
	}

	/* Unmask all wake up events in event1 */
	PMU_MASK_EVENT = ~PMU_MASK_EVENT_BIT_ALL;
	/* Mask events in event2 */
	PMU_MASK_EVENT2 = PMU_MASK2_ALL_EVENTS;

#if defined(CHIP_VARIANT_ISH5P4)
	SBEP_REG_CLK_GATE_ENABLE =
		(SB_CLK_GATE_EN_LOCAL_CLK_GATE | SB_CLK_GATE_EN_TRUNK_CLK_GATE);
#endif

	if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
		PMU_ISH_FABRIC_CNT = (PMU_ISH_FABRIC_CNT & 0xffff0000) |
				     FABRIC_IDLE_COUNT;
		PMU_PGCB_CLKGATE_CTRL = TRUNK_CLKGATE_COUNT;
	}

	if (IS_ENABLED(CONFIG_ISH_PM_RESET_PREP)) {
		/* unmask reset prep avail interrupt */
		PMU_RST_PREP = 0;

		task_enable_irq(ISH_RESET_PREP_IRQ);
	}

	if (IS_ENABLED(CONFIG_ISH_PM_D3)) {
		/* unmask D3 and BME interrupts */
		PMU_D3_STATUS &= (PMU_D3_BIT_SET | PMU_BME_BIT_SET);

		if ((!(PMU_D3_STATUS & PMU_D3_BIT_SET)) &&
		    (PMU_D3_STATUS & PMU_BME_BIT_SET))
			PMU_D3_STATUS = PMU_D3_STATUS;

#if defined(CHIP_VARIANT_ISH5P4)
		if (IS_ENABLED(CONFIG_ISH_NEW_PM)) {
			/* Mask all function1 */
			PMU_REG_MASK_D3_RISE = 0x2;
			PMU_REG_MASK_D3_FALL = 0x2;
			PMU_REG_MASK_BME_RISE = 0x2;
			PMU_REG_MASK_BME_FALL = 0x2;
		}
#endif
		enable_d3bme_irqs();
	}
}

__noreturn void ish_pm_reset(enum ish_pm_state pm_state)
{
	if (IS_ENABLED(CONFIG_ISH_PM_AONTASK) && pm_ctx.aon_valid) {
		handle_reset_in_aontask(pm_state);
	} else {
		ish_mia_reset();
	}

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
	pm_ctx.console_expire_time.val =
		get_time().val + CONSOLE_IN_USE_ON_BOOT_TIME;

	while (1) {
		t0 = get_time();
		next_delay = __hw_clock_event_get() - t0.le.lo;

		/*
		 * Most of the time, 'next_delay' will be positive. But, due to
		 * interrupt latency, it's possible that get_time() returns
		 * the value bigger than the one from __hw_clock_event_get()
		 * which is supposed to be updated by ISR before control reaches
		 * to the get_time().
		 *
		 * Here, we handle the delayed update by changing negative to 0.
		 */
		pm_process(t0, MAX(0, next_delay));
	}
}

/*
 * helper for command_idle_stats
 */
static void print_stats(const char *name, const struct pm_stat *stat)
{
	if (stat->count)
		ccprintf("    %s:\n"
			 "        counts: %llu\n"
			 "        time:   %.6llus\n",
			 name, stat->count, stat->total_time_us);
}

/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, const char **argv)
{
	struct ish_aon_share *aon_share = pm_ctx.aon_share;

	ccprintf("Aontask exists: %s\n", pm_ctx.aon_valid ? "Yes" : "No");
	ccprintf("Total time on: %.6llus\n", get_time().val);
	ccprintf("Idle sleep:\n");
	print_stats("D0i0", &pm_stats.d0i0);

	ccprintf("Deep sleep:\n");
	print_stats("D0i1", &pm_stats.d0i1);
	print_stats("D0i2", &pm_stats.d0i2);
	print_stats("D0i3", &pm_stats.d0i3);
	if (IS_ENABLED(CONFIG_ISH_IPAPG))
		print_stats("IPAPG", &pm_stats.pg);

	if (pm_ctx.aon_valid) {
		ccprintf("    Aontask status:\n");
		ccprintf("        last error:   %u\n", aon_share->last_error);
		ccprintf("        error counts: %u\n", aon_share->error_count);
	}

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print power management statistics");

/**
 * main FW only need handle PMU wakeup interrupt for D0i1 state, aontask will
 * handle PMU wakeup interrupt for other low power states
 */
__maybe_unused static void pmu_wakeup_isr(void)
{
	/* at current nothing need to do */
}

#ifdef CONFIG_ISH_PM_D0I1
DECLARE_IRQ(ISH_PMU_WAKEUP_IRQ, pmu_wakeup_isr);
#endif

/**
 * from ISH5.0, when system doing S0->Sx transition, will receive reset prep
 * interrupt, will switch to aontask for handling
 *
 */

__maybe_unused __noreturn static void reset_prep_isr(void)
{
	/* mask reset prep avail interrupt */
	PMU_RST_PREP = PMU_RST_PREP_INT_MASK;

	/*
	 * Indicate completion of servicing the interrupt to IOAPIC first
	 * then indicate completion of servicing the interrupt to LAPIC
	 */
	IOAPIC_EOI_REG = ISH_RESET_PREP_VEC;
	LAPIC_EOI_REG = 0x0;

	system_reset(0);
	__builtin_unreachable();
}

#ifdef CONFIG_ISH_PM_RESET_PREP
DECLARE_IRQ(ISH_RESET_PREP_IRQ, reset_prep_isr);
#endif

__maybe_unused static void handle_d3(uint32_t irq_vec)
{
	PMU_D3_STATUS = PMU_D3_STATUS;

	if (PMU_D3_STATUS & (PMU_D3_BIT_RISING_EDGE_STATUS | PMU_D3_BIT_SET)) {
		/*
		 * Indicate completion of servicing the interrupt to IOAPIC
		 * first then indicate completion of servicing the interrupt
		 * to LAPIC
		 */
		IOAPIC_EOI_REG = irq_vec;
		LAPIC_EOI_REG = 0x0;

		ish_persistent_data_commit();
		ish_pm_reset(ISH_PM_STATE_D3);
	}
}

static void d3_rise_isr(void)
{
	handle_d3(ISH_D3_RISE_VEC);
}

static __maybe_unused void d3_fall_isr(void)
{
	handle_d3(ISH_D3_FALL_VEC);
}

static __maybe_unused void bme_rise_isr(void)
{
	handle_d3(ISH_BME_RISE_VEC);
}

static __maybe_unused void bme_fall_isr(void)
{
	handle_d3(ISH_BME_FALL_VEC);
}

#ifdef CONFIG_ISH_PM_D3
DECLARE_IRQ(ISH_D3_RISE_IRQ, d3_rise_isr);
#ifndef CONFIG_ISH_NEW_PM
DECLARE_IRQ(ISH_D3_FALL_IRQ, d3_fall_isr);
DECLARE_IRQ(ISH_BME_RISE_IRQ, bme_rise_isr);
DECLARE_IRQ(ISH_BME_FALL_IRQ, bme_fall_isr);
#endif
#endif

void ish_pm_refresh_console_in_use(void)
{
	disable_sleep(SLEEP_MASK_CONSOLE);

	/* Set console in use expire time. */
	pm_ctx.console_expire_time = get_time();
	pm_ctx.console_expire_time.val +=
		pm_ctx.console_in_use_timeout_sec * SECOND;
}
