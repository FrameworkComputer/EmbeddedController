/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#include "atomic.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "system.h"
#include "uart.h"
#include "util.h"
#include "task.h"
#include "timer.h"

#define TIMER_SYSJUMP_TAG 0x4d54  /* "TM" */

/* High word of the 64-bit timestamp counter  */
static volatile uint32_t clksrc_high;

/* Bitmap of currently running timers */
static uint32_t timer_running;

/* Deadlines of all timers */
static timestamp_t timer_deadline[TASK_ID_COUNT];
static uint32_t next_deadline = 0xffffffff;

/* Hardware timer routine IRQ number */
static int timer_irq;

static void expire_timer(task_id_t tskid)
{
	/* we are done with this timer */
	atomic_clear(&timer_running, 1 << tskid);
	/* wake up the taks waiting for this timer */
	task_set_event(tskid, TASK_EVENT_TIMER, 0);
}

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((int64_t)(now->val - deadline.val) >= 0);
}

void process_timers(int overflow)
{
	uint32_t check_timer, running_t0;
	timestamp_t next;
	timestamp_t now;

	if (overflow)
		clksrc_high++;

	do {
		next.val = -1ull;
		now = get_time();
		do {
			/* read atomically the current state of timer running */
			check_timer = running_t0 = timer_running;
			while (check_timer) {
				int tskid = 31 - __builtin_clz(check_timer);

				/* timer has expired ? */
				if (timer_deadline[tskid].val <= now.val)
					expire_timer(tskid);
				else if ((timer_deadline[tskid].le.hi ==
					  now.le.hi) &&
					 (timer_deadline[tskid].le.lo <
					  next.le.lo))
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

		__hw_clock_event_set(next.le.lo);
		next_deadline = next.le.lo;
	} while (next.val <= get_time().val);
}

#ifndef CONFIG_HW_SPECIFIC_UDELAY
void udelay(unsigned us)
{
	unsigned t0 = __hw_clock_source_read();

	/*
	 * udelay() may be called with interrupts disabled, so we can't rely on
	 * process_timers() updating the top 32 bits.  So handle wraparound
	 * ourselves rather than calling get_time() and comparing with a
	 * deadline.
	 *
	 * This may fail for delays close to 2^32 us (~4000 sec), because the
	 * subtraction below can overflow.  That's acceptable, because the
	 * watchdog timer would have tripped long before that anyway.
	 */
	while (__hw_clock_source_read() - t0 <= us)
		;
}
#endif

int timer_arm(timestamp_t tstamp, task_id_t tskid)
{
	ASSERT(tskid < TASK_ID_COUNT);

	if (timer_running & (1<<tskid))
		return EC_ERROR_BUSY;

	timer_deadline[tskid] = tstamp;
	atomic_or(&timer_running, 1<<tskid);

	/* Modify the next event if needed */
	if ((tstamp.le.hi < clksrc_high) ||
	    ((tstamp.le.hi == clksrc_high) && (tstamp.le.lo <= next_deadline)))
		task_trigger_irq(timer_irq);

	return EC_SUCCESS;
}

void timer_cancel(task_id_t tskid)
{
	ASSERT(tskid < TASK_ID_COUNT);

	atomic_clear(&timer_running, 1 << tskid);
	/*
	 * Don't need to cancel the interrupt: it would be slow, just do it on
	 * the next IT
	 */
}

/*
 * For us < (2^31 - task scheduling latency)(~ 2147 sec), this function will
 * sleep for at least us, and no more than 2*us. As us approaches 2^32-1, the
 * probability of delay longer than 2*us (and possibly infinite delay)
 * increases.
 */
void usleep(unsigned us)
{
	uint32_t evt = 0;
	uint32_t t0 = __hw_clock_source_read();

	/* If task scheduling has not started, just delay */
	if (!task_start_called()) {
		udelay(us);
		return;
	}

	ASSERT(us);
	do {
		evt |= task_wait_event(us);
	} while (!(evt & TASK_EVENT_TIMER) &&
		((__hw_clock_source_read() - t0) < us));

	/* Re-queue other events which happened in the meanwhile */
	if (evt)
		atomic_or(task_get_event_bitmap(task_get_current()),
			  evt & ~TASK_EVENT_TIMER);
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

void force_time(timestamp_t ts)
{
	clksrc_high = ts.le.hi;
	__hw_clock_source_set(ts.le.lo);
	/* some timers might be already expired : process them */
	task_trigger_irq(timer_irq);
}

#ifdef CONFIG_CMD_TIMERINFO
void timer_print_info(void)
{
	uint64_t t = get_time().val;
	uint64_t deadline = (uint64_t)clksrc_high << 32 |
		__hw_clock_event_get();
	int tskid;

	ccprintf("Time:     0x%016lx us\n"
		 "Deadline: 0x%016lx -> %11.6ld s from now\n"
		 "Active timers:\n",
		 t, deadline, deadline - t);
	cflush();

	for (tskid = 0; tskid < TASK_ID_COUNT; tskid++) {
		if (timer_running & (1<<tskid)) {
			ccprintf("  Tsk %2d  0x%016lx -> %11.6ld\n", tskid,
				 timer_deadline[tskid].val,
				 timer_deadline[tskid].val - t);
			cflush();
		}
	}
}
#else
void timer_print_info(void) { }
#endif

void timer_init(void)
{
	const timestamp_t *ts;
	int size, version;

	BUILD_ASSERT(TASK_ID_COUNT < sizeof(timer_running) * 8);

	/* Restore time from before sysjump */
	ts = (const timestamp_t *)system_get_jump_tag(TIMER_SYSJUMP_TAG,
						      &version, &size);
	if (ts && version == 1 && size == sizeof(timestamp_t)) {
		clksrc_high = ts->le.hi;
		timer_irq = __hw_clock_source_init(ts->le.lo);
	} else {
		clksrc_high = 0;
		timer_irq = __hw_clock_source_init(0);
	}
}

/* Preserve time across a sysjump */
static void timer_sysjump(void)
{
	timestamp_t ts = get_time();

	system_add_jump_tag(TIMER_SYSJUMP_TAG, 1, sizeof(ts), &ts);
}
DECLARE_HOOK(HOOK_SYSJUMP, timer_sysjump, HOOK_PRIO_DEFAULT);

static int command_wait(int argc, char **argv)
{
	char *e;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	i = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	udelay(i * 1000);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(waitms, command_wait,
			"msec",
			"Busy-wait for msec",
			NULL);

#ifdef CONFIG_CMD_FORCETIME
static int command_force_time(int argc, char **argv)
{
	char *e;
	timestamp_t new;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	new.le.hi = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	new.le.lo = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	ccprintf("Time: 0x%016lx = %.6ld s\n", new.val, new.val);
	force_time(new);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forcetime, command_force_time,
			"hi lo",
			"Force current time",
			NULL);
#endif

static int command_get_time(int argc, char **argv)
{
	timestamp_t ts = get_time();
	ccprintf("Time: 0x%016lx = %.6ld s\n", ts.val, ts.val);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gettime, command_get_time,
			NULL,
			"Print current time",
			NULL);

#ifdef CONFIG_CMD_TIMERINFO
int command_timer_info(int argc, char **argv)
{
	timer_print_info();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(timerinfo, command_timer_info,
			NULL,
			"Print timer info",
			NULL);
#endif
