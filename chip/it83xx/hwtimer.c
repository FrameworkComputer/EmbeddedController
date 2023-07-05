/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "common.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "intc.h"
#include "irq_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/*
 * The IT839X series support combinational mode for combining specific pairs of
 * timers: 3(24-bit) and 4(32-bit) / timer 5(24-bit) and 6(32-bit) /
 * timer 7(24-bit) and 8(32-bit).
 *
 * 32-bit MHz free-running counter: We combine (bit3@IT83XX_ETWD_ETXCTRL)
 * timer 3(TIMER_L) and 4(TIMER_H) and set clock source register to 8MHz.
 * In combinational mode, the counter register(IT83XX_ETWD_ETXCNTLR) of timer 3
 * is a fixed value = 7, and observation register(IT83XX_ETWD_ETXCNTOR)
 * of timer 4 will increase one per-us.
 *
 * For example, if
 * __hw_clock_source_set() set 0 us, the counter setting registers are
 * timer 3(TIMER_L) = 0x000007 (fixed, will not change)
 * timer 4(TIMER_H) = 0xffffffff
 *
 * Note:
 * In combinational mode, the counter observation value of
 * timer 4(TIMER_H), 6, 8 will in incrementing order.
 * For the above example, the counter observation value registers will be
 * timer 3(TIMER_L) 0x0000007
 * timer 4(TIMER_H) ~0xffffffff = 0x00000000
 *
 * The following will describe timer 3 and 4's operation in combinational mode:
 * 1. When timer 3(TIMER_L) has completed each counting (per-us),
      timer 4(TIMER_H) observation value++.
 * 2. When timer 4(TIMER_H) observation value overflows:
 *    timer 4(TIMER_H) observation value = ~counter setting register.
 * 3. Timer 4(TIMER_H) interrupt occurs.
 *
 * IT839X only supports terminal count interrupt. We need a separate
 * 8 MHz 32-bit timer to handle events.
 */

#define MS_TO_COUNT(hz, ms) ((hz) * (ms) / 1000)

const struct ext_timer_ctrl_t et_ctrl_regs[] = {
	{ &IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, &IT83XX_INTC_ISR19, 0x08,
	  IT83XX_IRQ_EXT_TIMER3 },
	{ &IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, &IT83XX_INTC_ISR19, 0x10,
	  IT83XX_IRQ_EXT_TIMER4 },
	{ &IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, &IT83XX_INTC_ISR19, 0x20,
	  IT83XX_IRQ_EXT_TIMER5 },
	{ &IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, &IT83XX_INTC_ISR19, 0x40,
	  IT83XX_IRQ_EXT_TIMER6 },
	{ &IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, &IT83XX_INTC_ISR19, 0x80,
	  IT83XX_IRQ_EXT_TIMER7 },
	{ &IT83XX_INTC_IELMR10, &IT83XX_INTC_IPOLR10, &IT83XX_INTC_ISR10, 0x01,
	  IT83XX_IRQ_EXT_TMR8 },
};
BUILD_ASSERT(ARRAY_SIZE(et_ctrl_regs) == EXT_TIMER_COUNT);

static void free_run_timer_overflow(void)
{
	/*
	 * If timer 4 (TIMER_H) counter register != 0xffffffff.
	 * This usually happens once after sysjump, force time, and etc.
	 * (when __hw_clock_source_set is called and param 'ts' != 0)
	 */
	if (IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) != 0xffffffff) {
		/* set timer counter register */
		IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) = 0xffffffff;
		/* bit[1], timer reset */
		IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) |= BIT(1);
	}
	/* w/c interrupt status */
	task_clear_pending_irq(et_ctrl_regs[FREE_EXT_TIMER_H].irq);
	/* timer overflow */
	process_timers(1);
	update_exc_start_time();
}

static void event_timer_clear_pending_isr(void)
{
	/* w/c interrupt status */
	task_clear_pending_irq(et_ctrl_regs[EVENT_EXT_TIMER].irq);
}

uint32_t __ram_code __hw_clock_source_read(void)
{
#ifdef IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
	/*
	 * In combinational mode, the counter observation register of
	 * timer 4(TIMER_H) will increment.
	 */
	return ext_observation_reg_read(FREE_EXT_TIMER_H);
#else
	return IT83XX_ETWD_ETXCNTOR(FREE_EXT_TIMER_H);
#endif
}

void __hw_clock_source_set(uint32_t ts)
{
	/* counting down timer, microseconds to timer counter register */
	IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) = 0xffffffff - ts;
	/* bit[1], timer reset */
	IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) |= BIT(1);
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t wait;
	/* bit0, disable event timer */
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) &= ~BIT(0);
	/* w/c interrupt status */
	event_timer_clear_pending_isr();
	/* microseconds to timer counter */
	wait = deadline - __hw_clock_source_read();
	IT83XX_ETWD_ETXCNTLR(EVENT_EXT_TIMER) =
		wait < EVENT_TIMER_COUNT_TO_US(0xffffffff) ?
			EVENT_TIMER_US_TO_COUNT(wait) :
			0xffffffff;
	/* enable and re-start timer */
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) |= 0x03;
	task_enable_irq(et_ctrl_regs[EVENT_EXT_TIMER].irq);
}

uint32_t __hw_clock_event_get(void)
{
	uint32_t next_event_us = __hw_clock_source_read();

	/* bit0, event timer is enabled */
	if (IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) & BIT(0)) {
		/* timer counter observation value to microseconds */
		next_event_us += EVENT_TIMER_COUNT_TO_US(
#ifdef IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
			ext_observation_reg_read(EVENT_EXT_TIMER));
#else
			IT83XX_ETWD_ETXCNTOR(EVENT_EXT_TIMER));
#endif
	}
	return next_event_us;
}

void __hw_clock_event_clear(void)
{
	/* stop event timer */
	ext_timer_stop(EVENT_EXT_TIMER, 1);
	event_timer_clear_pending_isr();
}

int __hw_clock_source_init(uint32_t start_t)
{
	/* bit3, timer 3 and timer 4 combinational mode */
	IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) |= BIT(3);
	/* init free running timer (timer 4, TIMER_H), clock source is 8mhz */
	ext_timer_ms(FREE_EXT_TIMER_H, EXT_PSR_8M_HZ, 0, 1, 0xffffffff, 1, 1);
	/* 1us counter setting (timer 3, TIMER_L) */
	ext_timer_ms(FREE_EXT_TIMER_L, EXT_PSR_8M_HZ, 1, 0, 7, 1, 1);
	__hw_clock_source_set(start_t);
	/* init event timer */
	ext_timer_ms(EVENT_EXT_TIMER, EXT_PSR_8M_HZ, 0, 0, 0xffffffff, 1, 1);
	/* returns the IRQ number of event timer */
	return et_ctrl_regs[EVENT_EXT_TIMER].irq;
}

static void __hw_clock_source_irq(void)
{
	/* Determine interrupt number. */
	int irq = intc_get_ec_int();

	/* SW/HW interrupt of event timer. */
	if (irq == et_ctrl_regs[EVENT_EXT_TIMER].irq) {
		IT83XX_ETWD_ETXCNTLR(EVENT_EXT_TIMER) = 0xffffffff;
		IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) |= BIT(1);
		event_timer_clear_pending_isr();
		process_timers(0);
		return;
	}

#ifdef CONFIG_WATCHDOG
	/*
	 * Both the external timer for the watchdog warning and the HW timer
	 * go through this irq. So, if this interrupt was caused by watchdog
	 * warning timer, then call that function.
	 */
	if (irq == et_ctrl_regs[WDT_EXT_TIMER].irq) {
		watchdog_warning_irq();
		return;
	}
#endif

#ifdef CONFIG_FANS
	if (irq == et_ctrl_regs[FAN_CTRL_EXT_TIMER].irq) {
		fan_ext_timer_interrupt();
		return;
	}
#endif

#ifdef CONFIG_CEC_BITBANG
	if (irq == et_ctrl_regs[CEC_EXT_TIMER].irq) {
		cec_ext_timer_interrupt(CEC_EXT_TIMER);
		return;
	}
#endif

	/* Interrupt of free running timer TIMER_H. */
	if (irq == et_ctrl_regs[FREE_EXT_TIMER_H].irq) {
		free_run_timer_overflow();
		return;
	}

	/*
	 * This interrupt is used to wakeup EC from sleep mode
	 * to complete PLL frequency change.
	 */
	if (irq == et_ctrl_regs[LOW_POWER_EXT_TIMER].irq) {
		ext_timer_stop(LOW_POWER_EXT_TIMER, 1);
		return;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_3, __hw_clock_source_irq, 1);

#ifdef IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
/* Number of CPU cycles in 125 us */
#define CYCLES_125NS (125 * (PLL_CLOCK / SECOND) / 1000)
uint32_t __ram_code ext_observation_reg_read(enum ext_timer_sel ext_timer)
{
	uint32_t prev_mask = read_clear_int_mask();
	uint32_t val;

	asm volatile(
		/* read observation register for the first time */
		"lwi %0,[%1]\n\t"
		/*
		 * the delay time between reading the first and second
		 * observation registers need to be greater than 0.125us and
		 * smaller than 0.250us.
		 */
		".rept %2\n\t"
		"nop\n\t"
		".endr\n\t"
		/* read for the second time */
		"lwi %0,[%1]\n\t"
		: "=&r"(val)
		: "r"((uintptr_t)&IT83XX_ETWD_ETXCNTOR(ext_timer)),
		  "i"(CYCLES_125NS));
	/* restore interrupts */
	set_int_mask(prev_mask);

	return val;
}
#endif

void ext_timer_start(enum ext_timer_sel ext_timer, int en_irq)
{
	/* enable external timer n */
	IT83XX_ETWD_ETXCTRL(ext_timer) |= 0x03;

	if (en_irq) {
		task_clear_pending_irq(et_ctrl_regs[ext_timer].irq);
		task_enable_irq(et_ctrl_regs[ext_timer].irq);
	}
}

void ext_timer_stop(enum ext_timer_sel ext_timer, int dis_irq)
{
	/* disable external timer n */
	IT83XX_ETWD_ETXCTRL(ext_timer) &= ~0x01;

	if (dis_irq)
		task_disable_irq(et_ctrl_regs[ext_timer].irq);
}

static void ext_timer_ctrl(enum ext_timer_sel ext_timer,
			   enum ext_timer_clock_source ext_timer_clock,
			   int start, int with_int, int32_t count)
{
	uint8_t intc_mask;

	/* rising-edge-triggered */
	intc_mask = et_ctrl_regs[ext_timer].mask;
	*et_ctrl_regs[ext_timer].mode |= intc_mask;
	*et_ctrl_regs[ext_timer].polarity &= ~intc_mask;

	/* clear interrupt status */
	task_clear_pending_irq(et_ctrl_regs[ext_timer].irq);

	/* These bits control the clock input source to the exttimer 3 - 8 */
	IT83XX_ETWD_ETXPSR(ext_timer) = ext_timer_clock;

	/* The count number of external timer n. */
	IT83XX_ETWD_ETXCNTLR(ext_timer) = count;

	ext_timer_stop(ext_timer, 0);
	if (start)
		ext_timer_start(ext_timer, 0);

	if (with_int)
		task_enable_irq(et_ctrl_regs[ext_timer].irq);
	else
		task_disable_irq(et_ctrl_regs[ext_timer].irq);
}

int ext_timer_ms(enum ext_timer_sel ext_timer,
		 enum ext_timer_clock_source ext_timer_clock, int start,
		 int with_int, int32_t ms, int first_time_enable, int raw)
{
	uint32_t count;

	if (raw) {
		count = ms;
	} else {
		if (ext_timer_clock == EXT_PSR_32P768K_HZ)
			count = MS_TO_COUNT(32768, ms);
		else if (ext_timer_clock == EXT_PSR_1P024K_HZ)
			count = MS_TO_COUNT(1024, ms);
		else if (ext_timer_clock == EXT_PSR_32_HZ)
			count = MS_TO_COUNT(32, ms);
		else if (ext_timer_clock == EXT_PSR_8M_HZ)
			count = 8000 * ms;
		else
			return -1;
	}

	if (count == 0)
		return -3;

	if (first_time_enable) {
		ext_timer_start(ext_timer, 0);
		ext_timer_stop(ext_timer, 0);
	}

	ext_timer_ctrl(ext_timer, ext_timer_clock, start, with_int, count);

	return 0;
}
