/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "cpu.h"
#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
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
 * That means we will have a 56-bit timer if timer 3(TIMER_L) and
 * timer 4(TIMER_H) is combined (bit3 @ IT83XX_ETWD_ETXCTRL).
 * For a 32-bit MHz free-running counter, we select 8MHz clock source
 * for timer 3(TIMER_L) and 4(TIMER_H).
 * Counter setting value register(IT83XX_ETWD_ETXCNTLR) and counter observation
 * value register(IT83XX_ETWD_ETXCNTOR) of timer 3 and 4 need to be shifted by
 * 3 (2^3). So that each count will be equal to 0.125us.
 *
 * For example, if
 * __hw_clock_source_set() set 0 us, the counter setting register are
 * timer 3(TIMER_L) ((0xffffffff << 3) & 0xffffff) = 0xfffff8
 * timer 4(TIMER_H) (0xffffffff >> (24-3)) = 0x000007ff
 * The 56-bit 8MHz timer = 0x000007fffffff8
 * 0x000007fffffff8 / (2^3) = 0xffffffff(32-bit MHz free-running counter)
 *
 * Note:
 * In combinational mode, the counter observation value of
 * timer 4(TIMER_H), 6, 8 will in incrementing order.
 * For the above example, the counter observation value registers will be
 * timer 3(TIMER_L) 0xfffff8
 * timer 4(TIMER_H) ~0x000007ff = 0xfffff800
 *
 * The following will describe timer 3 and 4's operation in combinational mode:
 * 1. When timer 3(TIMER_L) observation value counting down to 0,
      timer 4(TIMER_H) observation value++.
 * 2. Timer 3(TIMER_L) observation value = counter setting register.
 * 3. Timer 3(TIMER_L) interrupt occurs if interrupt is enabled.
 * 4. When timer 4(TIMER_H) observation value overflows.
 * 5. Timer 4(TIMER_H) observation value = ~counter setting register.
 * 6. Timer 4(TIMER_H) interrupt occurs.
 *
 * IT839X only supports terminal count interrupt. We need a separate
 * 8 MHz 32-bit timer to handle events.
 */

#define TIMER_COUNT_1US_SHIFT      3

/* Combinational mode, microseconds to timer counter setting register */
#define TIMER_H_US_TO_COUNT(us)  ((us) >> (24 - TIMER_COUNT_1US_SHIFT))
#define TIMER_L_US_TO_COUNT(us)  (((us) << TIMER_COUNT_1US_SHIFT) & 0x00ffffff)

/* Free running timer counter observation value to microseconds */
#define TIMER_H_COUNT_TO_US(cnt) ((~(cnt)) << (24 - TIMER_COUNT_1US_SHIFT))
#define TIMER_L_COUNT_TO_US(cnt) (((cnt) & 0x00ffffff) >> TIMER_COUNT_1US_SHIFT)

/* Microseconds to event timer counter setting register */
#define EVENT_TIMER_US_TO_COUNT(us)  ((us) << TIMER_COUNT_1US_SHIFT)
/* Event timer counter observation value to microseconds */
#define EVENT_TIMER_COUNT_TO_US(cnt) ((cnt) >> TIMER_COUNT_1US_SHIFT)

#define TIMER_H_CNT_COMP TIMER_H_US_TO_COUNT(0xffffffff)
#define TIMER_L_CNT_COMP TIMER_L_US_TO_COUNT(0xffffffff)

#define MS_TO_COUNT(hz, ms) ((hz) * (ms) / 1000)

const struct ext_timer_ctrl_t et_ctrl_regs[] = {
	{&IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, 0x08,
		IT83XX_IRQ_EXT_TIMER3},
	{&IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, 0x10,
		IT83XX_IRQ_EXT_TIMER4},
	{&IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, 0x20,
		IT83XX_IRQ_EXT_TIMER5},
	{&IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, 0x40,
		IT83XX_IRQ_EXT_TIMER6},
	{&IT83XX_INTC_IELMR19, &IT83XX_INTC_IPOLR19, 0x80,
		IT83XX_IRQ_EXT_TIMER7},
	{&IT83XX_INTC_IELMR10, &IT83XX_INTC_IPOLR10, 0x01,
		IT83XX_IRQ_EXT_TMR8},
};
BUILD_ASSERT(ARRAY_SIZE(et_ctrl_regs) == EXT_TIMER_COUNT);

static void free_run_timer_config_counter(uint32_t us)
{
	/*
	 * microseconds to timer counter,
	 * timer 3(TIMER_L) and 4(TIMER_H) combinational mode
	 */
	IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) = TIMER_H_US_TO_COUNT(us);
	IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_L) = TIMER_L_US_TO_COUNT(us);
	/* bit1, timer re-start */
	IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) |= (1 << 1);
}

static void free_run_timer_clear_pending_isr(void)
{
	/* w/c interrupt status */
	task_clear_pending_irq(et_ctrl_regs[FREE_EXT_TIMER_L].irq);
	task_clear_pending_irq(et_ctrl_regs[FREE_EXT_TIMER_H].irq);
}

static void free_run_timer_overflow(void)
{
	/*
	 * If timer counter 4(TIMER_H) + timer counter 3(TIMER_L)
	 * != 0x000007fffffff8.
	 * This usually happens once after sysjump, force time, and etc.
	 * (when __hw_clock_source_set is called and param 'ts' != 0)
	 */
	if ((IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) != TIMER_H_CNT_COMP) ||
		(IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_L) != TIMER_L_CNT_COMP))
		free_run_timer_config_counter(0xffffffff);

	/* w/c interrupt status */
	free_run_timer_clear_pending_isr();
	/* timer overflow */
	process_timers(1);
	update_exc_start_time();
}

static void event_timer_clear_pending_isr(void)
{
	/* w/c interrupt status */
	task_clear_pending_irq(et_ctrl_regs[EVENT_EXT_TIMER].irq);
}

uint32_t __hw_clock_source_read(void)
{
	uint32_t l_cnt, h_cnt;

	/*
	 * get timer counter observation value, timer 3(TIMER_L) and 4(TIMER_H)
	 * combinational mode.
	 */
	h_cnt = IT83XX_ETWD_ETXCNTOR(FREE_EXT_TIMER_H);
	l_cnt = IT83XX_ETWD_ETXCNTOR(FREE_EXT_TIMER_L);
	/* timer 3(TIMER_L) overflow, get counter observation value again */
	if (h_cnt != IT83XX_ETWD_ETXCNTOR(FREE_EXT_TIMER_H)) {
		h_cnt = IT83XX_ETWD_ETXCNTOR(FREE_EXT_TIMER_H);
		l_cnt = IT83XX_ETWD_ETXCNTOR(FREE_EXT_TIMER_L);
	}

	/* timer counter observation value to microseconds */
	return 0xffffffff - (TIMER_L_COUNT_TO_US(l_cnt) |
			TIMER_H_COUNT_TO_US(h_cnt));
}

void __hw_clock_source_set(uint32_t ts)
{
	uint32_t start_us;

	/* counting down timer */
	start_us = 0xffffffff - ts;

	/* timer 3(TIMER_L) and timer 4(TIMER_H) are not enabled */
	if ((IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) & 0x09) != 0x09) {
		/* bit3, timer 3 and timer 4 combinational mode */
		IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) |= (1 << 3);
		/* microseconds to timer counter, clock source is 8mhz */
		ext_timer_ms(FREE_EXT_TIMER_H, EXT_PSR_8M_HZ, 0, 1,
			TIMER_H_US_TO_COUNT(start_us), 1, 1);
		ext_timer_ms(FREE_EXT_TIMER_L, EXT_PSR_8M_HZ, 1, 1,
			TIMER_L_US_TO_COUNT(start_us), 1, 1);
	} else {
		free_run_timer_clear_pending_isr();
		/* set timer counter only */
		free_run_timer_config_counter(start_us);
		task_enable_irq(et_ctrl_regs[FREE_EXT_TIMER_H].irq);
		task_enable_irq(et_ctrl_regs[FREE_EXT_TIMER_L].irq);
	}
}

void __hw_clock_event_set(uint32_t deadline)
{
	uint32_t wait;
	/* bit0, disable event timer */
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) &= ~(1 << 0);
	/* w/c interrupt status */
	event_timer_clear_pending_isr();
	/* microseconds to timer counter */
	wait = deadline - __hw_clock_source_read();
	IT83XX_ETWD_ETXCNTLR(EVENT_EXT_TIMER) =
		wait < EVENT_TIMER_COUNT_TO_US(0xffffffff) ?
		EVENT_TIMER_US_TO_COUNT(wait) : 0xffffffff;
	/* enable and re-start timer */
	IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) |= 0x03;
	task_enable_irq(et_ctrl_regs[EVENT_EXT_TIMER].irq);
}

uint32_t __hw_clock_event_get(void)
{
	uint32_t next_event_us = __hw_clock_source_read();

	/* bit0, event timer is enabled */
	if (IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) & (1 << 0)) {
		/* timer counter observation value to microseconds */
		next_event_us += EVENT_TIMER_COUNT_TO_US(
			IT83XX_ETWD_ETXCNTOR(EVENT_EXT_TIMER));
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
	/* enable free running timer */
	__hw_clock_source_set(start_t);
	/* init event timer */
	ext_timer_ms(EVENT_EXT_TIMER, EXT_PSR_8M_HZ, 0, 0, 0xffffffff, 1, 1);
	/* returns the IRQ number of event timer */
	return et_ctrl_regs[EVENT_EXT_TIMER].irq;
}

static void __hw_clock_source_irq(void)
{
	/* Determine interrupt number. */
	int irq = IT83XX_INTC_IVCT3 - 16;

	/* SW/HW interrupt of event timer. */
	if ((get_sw_int() == et_ctrl_regs[EVENT_EXT_TIMER].irq) ||
		(irq == et_ctrl_regs[EVENT_EXT_TIMER].irq)) {
		IT83XX_ETWD_ETXCNTLR(EVENT_EXT_TIMER) = 0xffffffff;
		IT83XX_ETWD_ETXCTRL(EVENT_EXT_TIMER) |= (1 << 1);
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

	/* Interrupt of free running timer TIMER_L. */
	if (irq == et_ctrl_regs[FREE_EXT_TIMER_L].irq) {
		/* w/c interrupt status */
		task_clear_pending_irq(et_ctrl_regs[FREE_EXT_TIMER_L].irq);
		/* disable timer 3(TIMER_L) interrupt */
		task_disable_irq(et_ctrl_regs[FREE_EXT_TIMER_L].irq);
		/* No need to set timer counter */
		if (IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_L) == TIMER_L_CNT_COMP)
			return;
		/*
		 * If timer counter 3(TIMER_L) != 0xfffff8.
		 * This usually happens once after sysjump, force time, and etc.
		 * (when __hw_clock_source_set is called and param 'ts' != 0)
		 *
		 * The interrupt is used to make sure the counter of
		 * timer 3(TIMER_L) is
		 * 0xfffff8(TIMER_L_COUNT_TO_US(0xffffffff)).
		 */
		if (IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H)) {
			IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_L) =
				TIMER_L_US_TO_COUNT(0xffffffff);
			IT83XX_ETWD_ETXCNTLR(FREE_EXT_TIMER_H) -= 1;
			IT83XX_ETWD_ETXCTRL(FREE_EXT_TIMER_L) |= (1 << 1);
			update_exc_start_time();
		} else {
			free_run_timer_overflow();
		}
		return;
	}

	/* Interrupt of free running timer TIMER_H. */
	if (irq == et_ctrl_regs[FREE_EXT_TIMER_H].irq) {
		free_run_timer_overflow();
		return;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_3, __hw_clock_source_irq, 1);

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
		int start,
		int with_int,
		int32_t count)
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
		enum ext_timer_clock_source ext_timer_clock,
		int start,
		int with_int,
		int32_t ms,
		int first_time_enable,
		int raw)
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
