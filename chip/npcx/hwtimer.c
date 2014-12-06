/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware timers driver */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "hooks.h"
#include "hwtimer.h"
#include "hwtimer_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

/* (2^TICK_ITIM_DEPTH us) between 2 ticks of timer */
#define TICK_ITIM_DEPTH    16                     /* Depth of ITIM Unit: bits */
#define TICK_INTERVAL      (1 << TICK_ITIM_DEPTH) /* Unit: us */
#define TICK_INTERVAL_MASK (TICK_INTERVAL - 1)    /* Mask of interval */
#define TICK_ITIM_MAX_CNT  (TICK_INTERVAL - 1)    /* Maximum counter value */

/* 32-bits counter value */
static volatile uint32_t cur_cnt_us;
static volatile uint32_t pre_cnt_us;
/* Time when event will be expired unit:us */
static volatile uint32_t evt_expired_us;
/* 32-bits event counter */
static volatile uint32_t evt_cnt;
/* Debugger information */
#if DEBUG_TMR
static volatile uint32_t evt_cnt_us_dbg;
static volatile uint32_t cur_cnt_us_dbg;
#endif

/*****************************************************************************/
/* Internal functions */
void init_hw_timer(int itim_no, enum ITIM16_SOURCE_CLOCK_T source)
{
	/* Use internal 32K clock/APB2 for ITIM16 */
	UPDATE_BIT(NPCX_ITCTS(itim_no), NPCX_ITIM16_CKSEL,
			source != ITIM16_SOURCE_CLOCK_APB2);

	/* Clear timeout status */
	SET_BIT(NPCX_ITCTS(itim_no), NPCX_ITIM16_TO_STS);

	/* ITIM timeout interrupt enable */
	SET_BIT(NPCX_ITCTS(itim_no), NPCX_ITIM16_TO_IE);

	/* ITIM timeout wake-up enable */
	SET_BIT(NPCX_ITCTS(itim_no), NPCX_ITIM16_TO_WUE);
}

/*****************************************************************************/
/* HWTimer event handlers */
void __hw_clock_event_set(uint32_t deadline)
{
	float    inv_evt_tick = INT_32K_CLOCK/(float)SECOND;
	uint32_t evt_cnt_us;
	/* Is deadline min value? */
	if (evt_expired_us != 0 && evt_expired_us < deadline)
		return;

	/* mark min event value */
	evt_expired_us = deadline;
	evt_cnt_us = deadline - __hw_clock_source_read();
#if DEBUG_TMR
	evt_cnt_us_dbg = deadline - __hw_clock_source_read();
#endif

	/* Event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITIM16_ITEN);
	/*
	 * ITIM count down : event expired : Unit: 1/32768 sec
	 * It must exceed evt_expired_us for process_timers function
	 */
	evt_cnt = ((uint32_t)(evt_cnt_us*inv_evt_tick)+1)-1;
	if (evt_cnt > TICK_ITIM_MAX_CNT)
		evt_cnt = TICK_ITIM_MAX_CNT;
	NPCX_ITCNT16(ITIM_EVENT_NO) = evt_cnt;

	/* Event module enable */
	SET_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITIM16_ITEN);

	/* Enable interrupt of ITIM */
	task_enable_irq(ITIM16_INT(ITIM_EVENT_NO));
}

/* Returns the time-stamp of the next programmed event */
uint32_t __hw_clock_event_get(void)
{
	return evt_expired_us;
}

/* Returns time delay cause of deep idle */
uint32_t __hw_clock_get_sleep_time(void)
{
	float evt_tick = SECOND/(float)INT_32K_CLOCK;
	uint32_t sleep_time;
	uint32_t cnt = NPCX_ITCNT16(ITIM_EVENT_NO);

	interrupt_disable();
	/* Event has been triggered but timer ISR dosen't handle it */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITIM16_TO_STS))
		sleep_time = (uint32_t) (evt_cnt+1)*evt_tick;
	/* Event hasn't been triggered */
	else
		sleep_time = (uint32_t) (evt_cnt+1 - cnt)*evt_tick;
	interrupt_enable();

	return sleep_time;
}

/* Cancel the next event programmed by __hw_clock_event_set */
void __hw_clock_event_clear(void)
{
	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITIM16_ITEN);

	/* Disable interrupt of Event */
	task_disable_irq(ITIM16_INT(ITIM_EVENT_NO));

	/* Clear event parameters */
	evt_expired_us = 0;
	evt_cnt = 0;
}

/* Irq for hwtimer event */
void __hw_clock_event_irq(void)
{
	int delay;
	/* Clear timeout status for event */
	SET_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITIM16_TO_STS);

	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITIM16_ITEN);

	/* Disable interrupt of event */
	task_disable_irq(ITIM16_INT(ITIM_EVENT_NO));

	/* Workaround for tick interrupt latency */
	delay = evt_expired_us - __hw_clock_source_read();
	if (delay > 0)
		cur_cnt_us += delay;

	/* Clear event parameters */
	evt_expired_us = 0;
	evt_cnt = 0;

	/* handle upper driver */
	process_timers(0);
}
DECLARE_IRQ(ITIM16_INT(ITIM_EVENT_NO) , __hw_clock_event_irq, 1);


/*****************************************************************************/
/* HWTimer tick handlers */

/* Returns the value of the free-running counter used as clock. */
uint32_t __hw_clock_source_read(void)
{
	uint32_t us;
	uint32_t cnt = NPCX_ITCNT16(ITIM_TIME_NO);
	/* Is timeout expired? - but timer ISR dosen't handle it */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM_TIME_NO), NPCX_ITIM16_TO_STS))
		us = TICK_INTERVAL;
	else
		us = TICK_INTERVAL - cnt;

#if DEBUG_TMR
	cur_cnt_us_dbg = cur_cnt_us + us;
#endif
	return cur_cnt_us + us;
}

/* Override the current value of the hardware counter */
void __hw_clock_source_set(uint32_t ts)
{
	/* Set current time */
	cur_cnt_us = ts;
}

/* Irq for hwtimer tick */
void __hw_clock_source_irq(void)
{
	/* Is timeout trigger trigger? */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM_TIME_NO), NPCX_ITIM16_TO_STS)) {
		/* Clear timeout status*/
		SET_BIT(NPCX_ITCTS(ITIM_TIME_NO), NPCX_ITIM16_TO_STS);

		/* Store previous time counter value */
		pre_cnt_us = cur_cnt_us;
		/* Increase TICK_INTERVAL unit:us */
		cur_cnt_us += TICK_INTERVAL;

		/* Is 32-bits timer count overflow? */
		if (pre_cnt_us > cur_cnt_us)
			process_timers(1);

	} else { /* Handle soft trigger */
		process_timers(0);
	}
}
DECLARE_IRQ(NPCX_IRQ_ITIM16_1, __hw_clock_source_irq, 1);

static void update_prescaler(void)
{
	/*
	 * prescaler to time tick
	 * Ttick_unit = (PRE_8+1) * Tapb2_clk
	 * PRE_8 = (Ttick_unit/Tapb2_clk) -1
	 */
	NPCX_ITPRE(ITIM_TIME_NO)  = (clock_get_apb2_freq() / SECOND) - 1;
	/* Set event tick unit = 1/32768 sec */
	NPCX_ITPRE(ITIM_EVENT_NO) = 0;

}
DECLARE_HOOK(HOOK_FREQ_CHANGE, update_prescaler, HOOK_PRIO_DEFAULT);

int __hw_clock_source_init(uint32_t start_t)
{
	/*
	 * 1. Use ITIM16-1 as internal time reading
	 * 2. Use ITIM16-2 for event handling
	 */

	/* Enable clock for ITIM peripheral */
	clock_enable_peripheral(CGC_OFFSET_TIMER, CGC_TIMER_MASK,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* init tick & event timer first */
	init_hw_timer(ITIM_TIME_NO,  ITIM16_SOURCE_CLOCK_APB2);
	init_hw_timer(ITIM_EVENT_NO, ITIM16_SOURCE_CLOCK_32K);

	/* Set initial prescaler */
	update_prescaler();

	/* ITIM count down : TICK_INTERVAL expired*/
	NPCX_ITCNT16(ITIM_TIME_NO) = TICK_ITIM_MAX_CNT;

	/*
	 * Override the count with the start value now that counting has
	 * started.
	 */
	__hw_clock_source_set(start_t);

	/* ITIM module enable */
	SET_BIT(NPCX_ITCTS(ITIM_TIME_NO), NPCX_ITIM16_ITEN);

	/* Enable interrupt of ITIM */
	task_enable_irq(ITIM16_INT(ITIM_TIME_NO));

	return ITIM16_INT(ITIM_TIME_NO);
}
