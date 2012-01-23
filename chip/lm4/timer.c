/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#include <stdint.h>

#include "task.h"
#include "timer.h"
#include "atomic.h"
#include "board.h"
#include "console.h"
#include "uart.h"
#include "registers.h"
#include "util.h"

#define US_PER_SECOND 1000000

/* Divider to get microsecond for the clock */
#define CLOCKSOURCE_DIVIDER (CPU_CLOCK/US_PER_SECOND)

/* high word of the 64-bit timestamp counter  */
static volatile uint32_t clksrc_high;

/* bitmap of currently running timers */
static uint32_t timer_running = 0;

/* deadlines of all timers */
static timestamp_t timer_deadline[TASK_ID_COUNT];

static uint32_t next_deadline = 0xffffffff;

void __hw_clock_event_set(uint32_t deadline)
{
	/* set the match on the deadline */
	LM4_TIMER_TAMATCHR(6) = 0xffffffff - deadline;
	/* Set the match interrupt */
	LM4_TIMER_IMR(6) |= 0x10;
}

void __hw_clock_event_clear(void)
{
	/* Disable the match interrupt */
	LM4_TIMER_IMR(6) &= ~0x10;
}

static uint32_t __hw_clock_source_read(void)
{
	return 0xffffffff - LM4_TIMER_TAV(6);
}

static void expire_timer(task_id_t tskid)
{
	/* we are done with this timer */
	atomic_clear(&timer_running, 1<<tskid);
	/* wake up the taks waiting for this timer */
	task_send_msg(tskid, TASK_ID_TIMER, 0);
}

/**
 * Search the next deadline and program it in the timer hardware
 *
 * It returns a bitmap of expired timers.
 */
static void process_timers(void)
{
	uint32_t check_timer, running_t0;
	timestamp_t next;
	timestamp_t now;

reprocess_timers:
	next.val = 0xffffffffffffffff;
	now = get_time();
	do {
		/* read atomically the current state of timer running */
		check_timer = running_t0 = timer_running;
		while (check_timer) {
			int tskid = 31 - __builtin_clz(check_timer);

			/* timer has expired ? */
			if (timer_deadline[tskid].val < now.val)
				expire_timer(tskid);
			else if ((timer_deadline[tskid].le.hi == now.le.hi) &&
			         (timer_deadline[tskid].le.lo < next.le.lo))
				next.val = timer_deadline[tskid].val;

			check_timer &= ~(1 << tskid);
		}
	/* if there is a new timer, let's retry */
	} while (timer_running & ~running_t0);

	if (next.le.hi == 0xffffffff) {
		/* no deadline to set */
		__hw_clock_event_clear();
		next_deadline = 0xffffffff;
		return;
	}

	if (next.val <= get_time().val)
		goto reprocess_timers;
	__hw_clock_event_set(next.le.lo);
	next_deadline = next.le.lo;
	//TODO narrow race: deadline might have been reached before
}

static void __hw_clock_source_irq(void)
{
	uint32_t status = LM4_TIMER_RIS(6);

	/* clear interrupt */
	LM4_TIMER_ICR(6) = status;
	/* free running counter as overflowed */
	if (status & 0x01) {
		clksrc_high++;
	}
	/* Find expired timers and set the new timer deadline */
	process_timers();
}
DECLARE_IRQ(LM4_IRQ_TIMERW0A, __hw_clock_source_irq, 1);


static void __hw_clock_source_init(void)
{
	volatile uint32_t scratch __attribute__((unused));

	/* Use WTIMER0 (timer 6) configured as a free running counter with 1 us
	 * period */

	/* Enable WTIMER0 clock */
	LM4_SYSTEM_RCGCWTIMER |= 1;
	/* wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCWTIMER;

	/* Ensure timer is disabled : TAEN = TBEN = 0 */
	LM4_TIMER_CTL(6) &= ~0x101;
	/* Set overflow interrupt */
	LM4_TIMER_IMR(6) = 0x1;
	/* 32-bit timer mode */
	LM4_TIMER_CFG(6) = 4;
	/* set the prescaler to increment every microsecond */
	LM4_TIMER_TAPR(6) = CLOCKSOURCE_DIVIDER;
	/* Periodic mode, counting down */
	LM4_TIMER_TAMR(6) = 0x22;
	/* use the full 32-bits of the timer */
	LM4_TIMER_TAILR(6) = 0xffffffff;
	/* Starts counting in timer A */
	LM4_TIMER_CTL(6) |= 0x1;

	/* Enable interrupt */
	task_enable_irq(LM4_IRQ_TIMERW0A);
}


void udelay(unsigned us)
{
	timestamp_t deadline = get_time();

	deadline.val += us;
	while (get_time().val < deadline.val) {}
}

int timer_arm(timestamp_t tstamp, task_id_t tskid)
{
	ASSERT(tskid < TASK_ID_COUNT);

	if (timer_running & (1<<tskid))
		return EC_ERROR_BUSY;

	timer_deadline[tskid] = tstamp;
	atomic_or(&timer_running, 1<<tskid);

	/* modify the next event if needed */
	if ((tstamp.le.hi < clksrc_high) ||
	    ((tstamp.le.hi == clksrc_high) && (tstamp.le.lo <= next_deadline)))
		task_trigger_irq(LM4_IRQ_TIMERW0A);

	return EC_SUCCESS;
}


int timer_cancel(task_id_t tskid)
{
	ASSERT(tskid < TASK_ID_COUNT);

	atomic_clear(&timer_running, 1<<tskid);
	/* don't bother about canceling the interrupt:
	 * it would be slow, just do it on the next IT
	 */

	return EC_SUCCESS;
}


void usleep(unsigned us)
{
	uint32_t evt = 0;
	ASSERT(us);
	do {
		evt |= task_wait_msg(us);
	} while (!(evt & (1 << TASK_ID_TIMER)));
	/* re-queue other events which happened in the meanwhile */
	if (evt)
		atomic_or(task_get_event_bitmap(task_get_current()),
		          evt & ~(1 << TASK_ID_TIMER));
}


timestamp_t get_time(void)
{
	timestamp_t ts;
	ts.le.hi = clksrc_high;
	ts.le.lo = __hw_clock_source_read();
	if (ts.le.hi != clksrc_high) {
		ts.le.hi = clksrc_high;
		ts.le.lo = __hw_clock_source_read();
	}
	return ts;
}


static int command_wait(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_INVAL;

	udelay(atoi(argv[1]) * 1000);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(waitms, command_wait);


static int command_get_time(int argc, char **argv)
{
	timestamp_t ts = get_time();
	uart_printf("Time: 0x%08x%08x us\n", ts.le.hi, ts.le.lo);
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(gettime, command_get_time);


int command_timer_info(int argc, char **argv)
{
	timestamp_t ts = get_time();
	int tskid;

	uart_printf("Time:     0x%08x%08x us\n"
	            "Deadline: 0x%08x%08x us\n"
	            "Active timers:\n",
	            ts.le.hi, ts.le.lo, clksrc_high,
		    0xffffffff - LM4_TIMER_TAMATCHR(6));
	for (tskid = 0; tskid < TASK_ID_COUNT; tskid++) {
		if (timer_running & (1<<tskid))
			uart_printf("Tsk %d tmr 0x%08x%08x\n", tskid,
			            timer_deadline[tskid].le.hi,
			            timer_deadline[tskid].le.lo);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(timerinfo, command_timer_info);


int timer_init(void)
{
	BUILD_ASSERT(TASK_ID_COUNT < sizeof(timer_running) * 8);

	__hw_clock_source_init();

	return EC_SUCCESS;
}
