/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "math_util.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Depth of event timer */
#define TICK_EVT_DEPTH 16 /* Depth of event timer Unit: bits */
#define TICK_EVT_INTERVAL BIT(TICK_EVT_DEPTH) /* Unit: us */
#define TICK_EVT_INTERVAL_MASK (TICK_EVT_INTERVAL - 1) /* Mask of interval */
#define TICK_EVT_MAX_CNT (TICK_EVT_INTERVAL - 1) /* Maximum event counter */

/* Time when event will be expired unit:us */
static volatile uint32_t evt_expired_us;
/* 32-bits event counter */
static volatile uint32_t evt_cnt;
/* Debugger information */
#if DEBUG_TMR
static volatile uint32_t evt_cnt_us_dbg;
static volatile uint32_t cur_cnt_us_dbg;
#endif

#if !(DEBUG_TMR)
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ##args)
#endif

/*****************************************************************************/
/* Internal functions */
void init_hw_timer(int itim_no, enum ITIM_SOURCE_CLOCK_T source)
{
	/* Select which clock to use for this timer */
	UPDATE_BIT(NPCX_ITCTS(itim_no), NPCX_ITCTS_CKSEL,
		   source != ITIM_SOURCE_CLOCK_APB2);

	/* Clear timeout status */
	SET_BIT(NPCX_ITCTS(itim_no), NPCX_ITCTS_TO_STS);

	/* ITIM timeout interrupt enable */
	SET_BIT(NPCX_ITCTS(itim_no), NPCX_ITCTS_TO_IE);

	/* ITIM timeout wake-up enable */
	SET_BIT(NPCX_ITCTS(itim_no), NPCX_ITCTS_TO_WUE);
}

/*****************************************************************************/
/* HWTimer event handlers */
void __hw_clock_event_set(uint32_t deadline)
{
	fp_t inv_evt_tick = FLOAT_TO_FP(INT_32K_CLOCK / (float)SECOND);
	uint32_t evt_cnt_us, current;
	/* Is deadline min value? */
	if (evt_expired_us != 0 && evt_expired_us < deadline)
		return;

	/* mark min event value */
	evt_expired_us = deadline;

	current = __hw_clock_source_read();
	/* Deadline is behind current timer */
	if (deadline < current) {
		evt_cnt_us = 1;
	} else {
		evt_cnt_us = deadline - current;
	}
#if DEBUG_TMR
	evt_cnt_us_dbg = evt_cnt_us;
#endif

	/* Event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/*
	 * ITIM count down : event expired : Unit: 1/32768 sec
	 * It must exceed evt_expired_us for process_timers function
	 */
	evt_cnt = FP_TO_INT((fp_inter_t)(evt_cnt_us)*inv_evt_tick);
	if (evt_cnt > TICK_EVT_MAX_CNT) {
		CPRINTS("Event overflow! 0x%08x, us is %d", evt_cnt,
			evt_cnt_us);
		evt_cnt = TICK_EVT_MAX_CNT;
	}

	/* Wait for module disable to take effect before updating count */
	while (IS_BIT_SET(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN))
		;

	NPCX_ITCNT(ITIM_EVENT_NO) = MAX(evt_cnt, 1);

	/* Event module enable */
	SET_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/* Wait for module enable */
	while (!IS_BIT_SET(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN))
		;

	/* Enable interrupt of ITIM */
	task_enable_irq(ITIM_INT(ITIM_EVENT_NO));
}

/* Returns the time-stamp of the next programmed event */
uint32_t __hw_clock_event_get(void)
{
	if (evt_expired_us)
		return evt_expired_us;
	else /* No events. Give maximum deadline */
		return EVT_MAX_EXPIRED_US;
}

/* Get current counter value of event timer */
uint16_t __hw_clock_event_count(void)
{
	uint16_t cnt, cnt2;

	cnt = NPCX_ITCNT(ITIM_EVENT_NO);
	/* Wait for two consecutive equal values are read */
	while ((cnt2 = NPCX_ITCNT(ITIM_EVENT_NO)) != cnt)
		cnt = cnt2;

	return cnt;
}

/* Returns time delay cause of deep idle */
uint32_t __hw_clock_get_sleep_time(uint16_t pre_evt_cnt)
{
	fp_t evt_tick = FLOAT_TO_FP(SECOND / (float)INT_32K_CLOCK);
	uint32_t sleep_time;
	uint16_t cnt = __hw_clock_event_count();

	/* Event has been triggered but timer ISR doesn't handle it */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_TO_STS))
		sleep_time =
			FP_TO_INT((fp_inter_t)(pre_evt_cnt + 1) * evt_tick);
	/* Event hasn't been triggered */
	else
		sleep_time = FP_TO_INT((fp_inter_t)(pre_evt_cnt + 1 - cnt) *
				       evt_tick);

	return sleep_time;
}

/* Cancel the next event programmed by __hw_clock_event_set */
void __hw_clock_event_clear(void)
{
	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/* Disable interrupt of Event */
	task_disable_irq(ITIM_INT(ITIM_EVENT_NO));

	/* Clear event parameters */
	evt_expired_us = 0;
	evt_cnt = 0;
}

/* Irq for hwtimer event */
static void __hw_clock_event_irq(void)
{
	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/* Disable interrupt of event */
	task_disable_irq(ITIM_INT(ITIM_EVENT_NO));

	/* Clear timeout status for event */
	SET_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_TO_STS);

	/* Clear event parameters */
	evt_expired_us = 0;
	evt_cnt = 0;

	/* handle upper driver */
	process_timers(0);

#ifdef CONFIG_LOW_POWER_IDLE
	/*
	 * Set event for ITIM32 after process_timers() since no events set if
	 * event's deadline is over 32 bits but current source clock isn't.
	 * ITIM32 is based on apb2 and ec won't wake-up in deep-idle even if it
	 * expires.
	 */
	if (evt_expired_us == 0)
		__hw_clock_event_set(EVT_MAX_EXPIRED_US);
#endif
}
DECLARE_IRQ(ITIM_INT(ITIM_EVENT_NO), __hw_clock_event_irq, 3);

/*****************************************************************************/
/* HWTimer tick handlers */

/* Modify preload counter of source clock. */
void hw_clock_source_set_preload(uint32_t ts, uint8_t clear)
{
	/* ITIM32 module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_ITEN);
	CLEAR_BIT(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_CKSEL);

	/* Set preload counter to current time */
	NPCX_ITCNT_SYSTEM = TICK_ITIM32_MAX_CNT - ts;
	/* Clear timeout status or not */
	if (clear)
		SET_BIT(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_TO_STS);
	/* ITIM32 module enable */
	SET_BIT(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_ITEN);
}

/* Returns the value of the free-running counter used as clock. */
uint32_t __hw_clock_source_read(void)
{
	uint32_t cnt, cnt2;

	cnt = NPCX_ITCNT_SYSTEM;
	/*
	 * Wait for two consecutive equal values are read no matter
	 * ITIM's source clock is APB2 or 32K since mux's delay.
	 */
	while ((cnt2 = NPCX_ITCNT_SYSTEM) != cnt)
		cnt = cnt2;

#if DEBUG_TMR
	cur_cnt_us_dbg = TICK_ITIM32_MAX_CNT - cnt;
#endif
	return TICK_ITIM32_MAX_CNT - cnt;
}

/* Override the current value of the hardware counter */
void __hw_clock_source_set(uint32_t ts)
{
#if DEBUG_TMR
	cur_cnt_us_dbg = TICK_ITIM32_MAX_CNT - ts;
#endif
	hw_clock_source_set_preload(ts, 0);
}

/* Irq for hwtimer tick */
static void __hw_clock_source_irq(void)
{
	/* Is timeout trigger trigger? */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_TO_STS)) {
		/* Restore ITIM32 preload counter value to maximum value */
		hw_clock_source_set_preload(0, 1);
		/* 32-bits timer count overflow */
		process_timers(1);

	} else { /* Handle soft trigger */
		process_timers(0);
#ifdef CONFIG_LOW_POWER_IDLE
		/* Set event for ITIM32. Please see above for detail */
		if (evt_expired_us == 0)
			__hw_clock_event_set(EVT_MAX_EXPIRED_US);
#endif
	}
}
DECLARE_IRQ(ITIM_INT(ITIM_SYSTEM_NO), __hw_clock_source_irq, 3);

/* Handle ITIM32 overflow if interrupt is disabled */
void __hw_clock_handle_overflow(uint32_t clksrc_high)
{
	timestamp_t newtime;

	/* Overflow occurred? */
	if (!IS_BIT_SET(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_TO_STS))
		return;

	/* Clear timeout status */
	SET_BIT(NPCX_ITCTS(ITIM_SYSTEM_NO), NPCX_ITCTS_TO_STS);

	/*
	 * Restore ITIM32 preload counter value to maximum and execute
	 * process_timers() later in ISR by trigger software interrupt in
	 * force_time().
	 */
	newtime.le.hi = clksrc_high + 1;
	newtime.le.lo = 0;
	force_time(newtime);
}

static void update_prescaler(void)
{
	/*
	 * prescaler to time tick
	 * Ttick_unit = (PRE_8+1) * Tapb2_clk
	 * PRE_8 = (Ttick_unit/Tapb2_clk) -1
	 */
	NPCX_ITPRE(ITIM_SYSTEM_NO) = (clock_get_apb2_freq() / SECOND) - 1;
	/* Set event tick unit = 1/32768 sec */
	NPCX_ITPRE(ITIM_EVENT_NO) = 0;
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, update_prescaler, HOOK_PRIO_DEFAULT);

void __hw_early_init_hwtimer(uint32_t start_t)
{
	/*
	 * 1. Use ITIM16-1 as internal time reading
	 * 2. Use ITIM16-2 for event handling
	 */

	/* Enable clock for ITIM peripheral */
	clock_enable_peripheral(CGC_OFFSET_TIMER, CGC_TIMER_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* init tick & event timer first */
	init_hw_timer(ITIM_SYSTEM_NO, ITIM_SOURCE_CLOCK_APB2);
	init_hw_timer(ITIM_EVENT_NO, ITIM_SOURCE_CLOCK_32K);

	/* Set initial prescaler */
	update_prescaler();

	hw_clock_source_set_preload(start_t, 1);
}

/* Note that early_init_hwtimer() has already executed by this point */
int __hw_clock_source_init(uint32_t start_t)
{
	/*
	 * Override the count with the start value now that counting has
	 * started. Note that we may have already called this function from
	 * gpio_pre_init(), but only in the case where we expected a reset, so
	 * we should not get here in that case.
	 */
	__hw_early_init_hwtimer(start_t);

	/* Enable interrupt of ITIM */
	task_enable_irq(ITIM_INT(ITIM_SYSTEM_NO));

	return ITIM_INT(ITIM_SYSTEM_NO);
}
