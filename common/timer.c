/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Timer module for Chrome EC operating system */

#include "atomic.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/kernel.h> /* For k_usleep() */
#else
extern __error("k_usleep() should only be called from Zephyr code") int32_t
	k_usleep(int32_t);
#endif /* CONFIG_ZEPHYR */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

#define TIMER_SYSJUMP_TAG 0x4d54 /* "TM" */

#define USLEEP_WARNING_INTERVAL_MS (20 * MSEC)

/* High 32-bits of the 64-bit timestamp counter. */
STATIC_IF_NOT(CONFIG_HWTIMER_64BIT) volatile uint32_t clksrc_high;

/* Hardware timer routine IRQ number */
static int timer_irq;

#ifndef CONFIG_ZEPHYR
/* Bitmap of currently running timers */
static uint32_t timer_running;

BUILD_ASSERT((sizeof(timer_running) * 8) > TASK_ID_COUNT);

/* Deadlines of all timers */
static timestamp_t timer_deadline[TASK_ID_COUNT];
static uint32_t next_deadline = 0xffffffff;

static void expire_timer(task_id_t tskid)
{
	/* we are done with this timer */
	atomic_clear_bits((atomic_t *)&timer_running, 1 << tskid);
	/* wake up the taks waiting for this timer */
	task_set_event(tskid, TASK_EVENT_TIMER);
}

void process_timers(int overflow)
{
	uint32_t check_timer, running_t0;
	timestamp_t next;
	timestamp_t now;

	if (!IS_ENABLED(CONFIG_HWTIMER_64BIT) && overflow)
		clksrc_high++;

	do {
		next.val = -1ull;
		now = get_time();
		do {
			/* read atomically the current state of timer running */
			check_timer = running_t0 = timer_running;
			while (check_timer) {
				int tskid = __fls(check_timer);
				/* timer has expired ? */
				if (timer_deadline[tskid].val <= now.val)
					expire_timer(tskid);
				else if ((timer_deadline[tskid].le.hi ==
					  now.le.hi) &&
					 (timer_deadline[tskid].le.lo <
					  next.le.lo))
					next.val = timer_deadline[tskid].val;

				check_timer &= ~BIT(tskid);
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
#endif /* !defined(CONFIG_ZEPHYR) */

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((int64_t)(now->val - deadline.val) >= 0);
}

#ifndef CONFIG_HW_SPECIFIC_UDELAY
void udelay(unsigned int us)
{
	unsigned int t0 = __hw_clock_source_read();

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

/* Zephyr provides its own implementation in task shim */
#ifndef CONFIG_ZEPHYR
int timer_arm(timestamp_t event, task_id_t tskid)
{
	timestamp_t now = get_time();

	ASSERT(tskid < TASK_ID_COUNT);

	if (timer_running & BIT(tskid))
		return EC_ERROR_BUSY;

	timer_deadline[tskid] = event;
	atomic_or((atomic_t *)&timer_running, BIT(tskid));

	/* Modify the next event if needed */
	if ((event.le.hi < now.le.hi) ||
	    ((event.le.hi == now.le.hi) && (event.le.lo <= next_deadline)))
		task_trigger_irq(timer_irq);

	return EC_SUCCESS;
}

void timer_cancel(task_id_t tskid)
{
	ASSERT(tskid < TASK_ID_COUNT);

	atomic_clear_bits((atomic_t *)&timer_running, BIT(tskid));
	/*
	 * Don't need to cancel the hardware timer interrupt, instead do
	 * timer-related housekeeping when the next timer interrupt fires.
	 */
}
#endif

/*
 * For us < (2^31 - task scheduling latency)(~ 2147 sec), this function will
 * sleep for at least us, and no more than 2*us. As us approaches 2^32-1, the
 * probability of delay longer than 2*us (and possibly infinite delay)
 * increases.
 */
void usleep(unsigned int us)
{
	uint32_t evt = 0;
	uint32_t t0;

	/* If a wait is 0, return immediately. */
	if (!us)
		return;

	if (IS_ENABLED(CONFIG_ZEPHYR)) {
		while (us)
			us = k_usleep(us);
		return;
	}

	t0 = __hw_clock_source_read();

	/* If task scheduling has not started, just delay */
	if (!task_start_called()) {
		udelay(us);
		return;
	}

	/* If in interrupt context or interrupts are disabled, use udelay() */
	if (!is_interrupt_enabled() || in_interrupt_context()) {
		/* Avoid printing warning too frequently */
		static timestamp_t next_print_deadline = { .val = 0 };

		if (timestamp_expired(next_print_deadline, NULL)) {
			next_print_deadline.val =
				get_time().val + USLEEP_WARNING_INTERVAL_MS;
			CPRINTS("Sleeping not allowed");
		}

		udelay(us);
		return;
	}

	do {
		evt |= task_wait_event(us);
	} while (!(evt & TASK_EVENT_TIMER) &&
		 ((__hw_clock_source_read() - t0) < us));

	/* Re-queue other events which happened in the meanwhile */
	if (evt)
		atomic_or(task_get_event_bitmap(task_get_current()),
			  evt & ~TASK_EVENT_TIMER);
}

#ifdef CONFIG_ZTEST
timestamp_t *get_time_mock;
#endif /* CONFIG_ZTEST */

timestamp_t get_time(void)
{
	timestamp_t ts;

#ifdef CONFIG_ZTEST
	if (get_time_mock != NULL)
		return *get_time_mock;
#endif /* CONFIG_ZTEST */

	if (IS_ENABLED(CONFIG_HWTIMER_64BIT)) {
		ts.val = __hw_clock_source_read64();
	} else {
		ts.le.hi = clksrc_high;
		ts.le.lo = __hw_clock_source_read();
		/*
		 * TODO(b/213342294) If statement below doesn't catch overflows
		 * when interrupts are disabled or currently processed interrupt
		 * has higher priority.
		 */
		if (ts.le.hi != clksrc_high) {
			ts.le.hi = clksrc_high;
			ts.le.lo = __hw_clock_source_read();
		}
	}

	return ts;
}

clock_t clock(void)
{
	/* __hw_clock_source_read() returns a microsecond resolution timer.*/
	return (clock_t)__hw_clock_source_read() / 1000;
}

void force_time(timestamp_t ts)
{
	if (IS_ENABLED(CONFIG_HWTIMER_64BIT)) {
		__hw_clock_source_set64(ts.val);
	} else {
		/* Save current interrupt state */
		bool interrupt_enabled = is_interrupt_enabled();

		/*
		 * Updating timer shouldn't be interrupted (eg. when counter
		 * overflows) because it could lead to some unintended
		 * consequences. Please note that this function can be called
		 * with disabled or enabled interrupts so we need to restore
		 * the original state later.
		 */
		interrupt_disable();

		clksrc_high = ts.le.hi;
		__hw_clock_source_set(ts.le.lo);

		/* Restore original interrupt state */
		if (interrupt_enabled)
			interrupt_enable();
	}

	/* some timers might be already expired : process them */
	task_trigger_irq(timer_irq);
}

/*
 * Define versions of __hw_clock_source_read and __hw_clock_source_set
 * that wrap the 64-bit versions for chips with CONFIG_HWTIMER_64BIT.
 */
#ifdef CONFIG_HWTIMER_64BIT
__overridable uint32_t __hw_clock_source_read(void)
{
	return (uint32_t)__hw_clock_source_read64();
}

void __hw_clock_source_set(uint32_t ts)
{
	uint64_t current = __hw_clock_source_read64();

	__hw_clock_source_set64(((current >> 32) << 32) | ts);
}
#endif /* CONFIG_HWTIMER_64BIT */

void timer_print_info(void)
{
	timestamp_t t = get_time();
	uint64_t deadline = (uint64_t)t.le.hi << 32 | __hw_clock_event_get();

	ccprintf("Time:     0x%016llx us, %11.6lld s\n"
		 "Deadline: 0x%016llx -> %11.6lld s from now\n"
		 "Active timers:\n",
		 t.val, t.val, deadline, deadline - t.val);
	cflush();

#ifndef CONFIG_ZEPHYR
	for (int tskid = 0; tskid < TASK_ID_COUNT; tskid++) {
		if (timer_running & BIT(tskid)) {
			ccprintf("  Tsk %2d  0x%016llx -> %11.6lld\n", tskid,
				 timer_deadline[tskid].val,
				 timer_deadline[tskid].val - t.val);
			cflush();
		}
	}
#endif /* !defined(CONFIG_ZEPHYR) */
}

void timer_init(void)
{
	const timestamp_t *ts;
	int size, version;

	/* Restore time from before sysjump */
	ts = (const timestamp_t *)system_get_jump_tag(TIMER_SYSJUMP_TAG,
						      &version, &size);
	if (ts && version == 1 && size == sizeof(timestamp_t)) {
		if (IS_ENABLED(CONFIG_HWTIMER_64BIT)) {
			timer_irq = __hw_clock_source_init64(ts->val);
		} else {
			clksrc_high = ts->le.hi;
			timer_irq = __hw_clock_source_init(ts->le.lo);
		}
	} else {
		if (IS_ENABLED(CONFIG_HWTIMER_64BIT))
			timer_irq = __hw_clock_source_init64(0);
		else
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

#ifdef CONFIG_CMD_WAITMS
static int command_wait(int argc, const char **argv)
{
	char *e;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	i = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (i < 0)
		return EC_ERROR_PARAM1;

	/*
	 * Reload the watchdog so that issuing multiple small waitms commands
	 * quickly one after the other will not cause a reset.
	 *
	 * Reloading before waiting also allows for testing watchdog.
	 */
	watchdog_reload();

	/*
	 * Waiting for too long (e.g. 3s) will cause the EC to reset due to a
	 * watchdog timeout. This is intended behaviour and is in fact used by
	 * a FAFT test to check that the watchdog timer is working.
	 */
	udelay(i * 1000);

	return EC_SUCCESS;
}
/* Typically a large delay (e.g. 3s) will cause a reset */
DECLARE_CONSOLE_COMMAND(waitms, command_wait, "msec",
			"Busy-wait for msec (large delays will reset)");
#endif

#ifdef CONFIG_CMD_FORCETIME
/*
 * Force the hwtimer to a given time. This may have undesired consequences,
 * especially when going "backward" in time, because task deadlines are
 * left un-adjusted.
 */
static int command_force_time(int argc, const char **argv)
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

	ccprintf("Time: 0x%016llx = %.6lld s\n", new.val, new.val);
	force_time(new);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(forcetime, command_force_time, "hi lo",
			"Force current time");
#endif

#ifdef CONFIG_CMD_GETTIME
static int command_get_time(int argc, const char **argv)
{
	timestamp_t ts = get_time();
	ccprintf("Time: 0x%016llx = %.6lld s\n", ts.val, ts.val);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(gettime, command_get_time, NULL,
			     "Print current time");
#endif

#ifdef CONFIG_CMD_TIMERINFO
static int command_timer_info(int argc, const char **argv)
{
	timer_print_info();

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(timerinfo, command_timer_info, NULL,
			     "Print timer info");
#endif
