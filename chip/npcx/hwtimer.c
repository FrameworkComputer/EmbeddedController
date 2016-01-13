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
#include "math_util.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

/* Use ITIM32 as main hardware timer */
#define TICK_ITIM32_MAX_CNT  0xFFFFFFFF

/* Depth of event timer */
#define TICK_EVT_DEPTH         16 /* Depth of event timer Unit: bits */
#define TICK_EVT_INTERVAL      (1 << TICK_EVT_DEPTH) /* Unit: us */
#define TICK_EVT_INTERVAL_MASK (TICK_EVT_INTERVAL - 1) /* Mask of interval */
#define TICK_EVT_MAX_CNT     (TICK_EVT_INTERVAL - 1) /* Maximum event counter */

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
#define CPRINTS(format, args...) cprints(CC_CLOCK, format, ## args)
#endif

/*****************************************************************************/
/* Internal functions */
void init_hw_timer(int itim_no, enum ITIM_SOURCE_CLOCK_T source)
{
	/* Use internal 32K clock/APB2 for ITIM16 */
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
	fp_t inv_evt_tick = FLOAT_TO_FP(INT_32K_CLOCK/(float)SECOND);
	int32_t  evt_cnt_us;
	/* Is deadline min value? */
	if (evt_expired_us != 0 && evt_expired_us < deadline)
		return;

	/* mark min event value */
	evt_expired_us = deadline;
	evt_cnt_us = deadline - __hw_clock_source_read();
#if DEBUG_TMR
	evt_cnt_us_dbg = deadline - __hw_clock_source_read();
#endif
	/* Deadline is behind current timer */
	if (evt_cnt_us < 0)
		evt_cnt_us = 1;

	/* Event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);
	/*
	 * ITIM count down : event expired : Unit: 1/32768 sec
	 * It must exceed evt_expired_us for process_timers function
	 */
	evt_cnt = FP_TO_INT((fp_inter_t)(evt_cnt_us) * inv_evt_tick);
	if (evt_cnt > TICK_EVT_MAX_CNT) {
		CPRINTS("Event overflow! 0x%08x, us is %d\r\n",
				evt_cnt, evt_cnt_us);
		evt_cnt = TICK_EVT_MAX_CNT;
	}
	NPCX_ITCNT16(ITIM_EVENT_NO) = evt_cnt;

	/* Event module enable */
	SET_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/* Enable interrupt of ITIM */
	task_enable_irq(ITIM16_INT(ITIM_EVENT_NO));
}

/* Returns the time-stamp of the next programmed event */
uint32_t __hw_clock_event_get(void)
{
	return evt_expired_us;
}

/* Get current counter value of event timer */
uint32_t __hw_clock_event_count(void)
{
	return NPCX_ITCNT16(ITIM_EVENT_NO);
}

/* Returns time delay cause of deep idle */
uint32_t __hw_clock_get_sleep_time(uint32_t pre_evt_cnt)
{
	fp_t evt_tick = FLOAT_TO_FP(SECOND/(float)INT_32K_CLOCK);
	uint32_t sleep_time;
	uint32_t cnt = NPCX_ITCNT16(ITIM_EVENT_NO);

	/* Event has been triggered but timer ISR dosen't handle it */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_TO_STS))
		sleep_time = FP_TO_INT((fp_inter_t)(pre_evt_cnt+1) * evt_tick);
	/* Event hasn't been triggered */
	else
		sleep_time = FP_TO_INT((fp_inter_t)(pre_evt_cnt+1 - cnt) *
				       evt_tick);

	return sleep_time;
}

/* Cancel the next event programmed by __hw_clock_event_set */
void __hw_clock_event_clear(void)
{
	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/* Disable interrupt of Event */
	task_disable_irq(ITIM16_INT(ITIM_EVENT_NO));

	/* Clear event parameters */
	evt_expired_us = 0;
	evt_cnt = 0;
}

/* Irq for hwtimer event */
void __hw_clock_event_irq(void)
{
	/* Clear timeout status for event */
	SET_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_TO_STS);

	/* ITIM event module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM_EVENT_NO), NPCX_ITCTS_ITEN);

	/* Disable interrupt of event */
	task_disable_irq(ITIM16_INT(ITIM_EVENT_NO));

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
	uint32_t cnt = NPCX_ITCNT32;
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
	/* ITIM32 module disable */
	CLEAR_BIT(NPCX_ITCTS(ITIM32), NPCX_ITCTS_ITEN);
	/* Set current time */
	NPCX_ITCNT32 = TICK_ITIM32_MAX_CNT - ts;
	/* ITIM32 module enable */
	SET_BIT(NPCX_ITCTS(ITIM32), NPCX_ITCTS_ITEN);

}

/* Irq for hwtimer tick */
void __hw_clock_source_irq(void)
{
	/* Is timeout trigger trigger? */
	if (IS_BIT_SET(NPCX_ITCTS(ITIM32), NPCX_ITCTS_TO_STS)) {
		/* Clear timeout status*/
		SET_BIT(NPCX_ITCTS(ITIM32), NPCX_ITCTS_TO_STS);
		/* 32-bits timer count overflow */
		process_timers(1);

	} else { /* Handle soft trigger */
		process_timers(0);
	}
}
DECLARE_IRQ(NPCX_IRQ_ITIM32, __hw_clock_source_irq, 1);

static void update_prescaler(void)
{
	/*
	 * prescaler to time tick
	 * Ttick_unit = (PRE_8+1) * Tapb2_clk
	 * PRE_8 = (Ttick_unit/Tapb2_clk) -1
	 */
	NPCX_ITPRE(ITIM32)  = (clock_get_apb2_freq() / SECOND) - 1;
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
	init_hw_timer(ITIM32,  ITIM_SOURCE_CLOCK_APB2);
	init_hw_timer(ITIM_EVENT_NO, ITIM_SOURCE_CLOCK_32K);

	/* Set initial prescaler */
	update_prescaler();

	/* ITIM count down : TICK_ITIM32_MAX_CNT us expired */
	NPCX_ITCNT32 = TICK_ITIM32_MAX_CNT;

	/*
	 * Override the count with the start value now that counting has
	 * started.
	 */
	__hw_clock_source_set(start_t);

	/* ITIM module enable */
	SET_BIT(NPCX_ITCTS(ITIM32), NPCX_ITCTS_ITEN);

	/* Enable interrupt of ITIM */
	task_enable_irq(NPCX_IRQ_ITIM32);

	return NPCX_IRQ_ITIM32;
}
