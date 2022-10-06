/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System hooks for Chrome EC */

#include "atomic.h"
#include "console.h"
#include "hooks.h"
#include "link_defs.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_HOOK_DEBUG
#define CPUTS(outstr) cputs(CC_HOOK, outstr)
#define CPRINTS(format, args...) cprints(CC_HOOK, format, ##args)
#else
#define CPUTS(outstr)
#define CPRINTS(format, args...)
#endif

#define DEFERRED_FUNCS_COUNT (__deferred_funcs_end - __deferred_funcs)

struct hook_ptrs {
	const struct hook_data *start;
	const struct hook_data *end;
};

/*
 * Hook data start and end pointers for each type of hook.  Must be in same
 * order as enum hook_type.
 */
static const struct hook_ptrs hook_list[] = {
	{ __hooks_init, __hooks_init_end },
	{ __hooks_pre_freq_change, __hooks_pre_freq_change_end },
	{ __hooks_freq_change, __hooks_freq_change_end },
	{ __hooks_sysjump, __hooks_sysjump_end },
	{ __hooks_chipset_pre_init, __hooks_chipset_pre_init_end },
	{ __hooks_chipset_startup, __hooks_chipset_startup_end },
	{ __hooks_chipset_resume, __hooks_chipset_resume_end },
	{ __hooks_chipset_suspend, __hooks_chipset_suspend_end },
#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
	{ __hooks_chipset_resume_init, __hooks_chipset_resume_init_end },
	{ __hooks_chipset_suspend_complete,
	  __hooks_chipset_suspend_complete_end },
#endif
	{ __hooks_chipset_shutdown, __hooks_chipset_shutdown_end },
	{ __hooks_chipset_shutdown_complete,
	  __hooks_chipset_shutdown_complete_end },
	{ __hooks_chipset_hard_off, __hooks_chipset_hard_off_end },
	{ __hooks_chipset_reset, __hooks_chipset_reset_end },
	{ __hooks_ac_change, __hooks_ac_change_end },
	{ __hooks_lid_change, __hooks_lid_change_end },
	{ __hooks_tablet_mode_change, __hooks_tablet_mode_change_end },
	{ __hooks_base_attached_change, __hooks_base_attached_change_end },
	{ __hooks_pwrbtn_change, __hooks_pwrbtn_change_end },
	{ __hooks_battery_soc_change, __hooks_battery_soc_change_end },
#ifdef CONFIG_USB_SUSPEND
	{ __hooks_usb_change, __hooks_usb_change_end },
#endif
	{ __hooks_tick, __hooks_tick_end },
	{ __hooks_second, __hooks_second_end },
	{ __hooks_usb_pd_disconnect, __hooks_usb_pd_disconnect_end },
	{ __hooks_usb_pd_connect, __hooks_usb_pd_connect_end },
	{ __hooks_power_supply_change, __hooks_power_supply_change_end },
};

/* Times for deferrable functions */
static int hook_task_started;

#ifdef CONFIG_HOOK_DEBUG
/* Stats for hooks */
static uint64_t max_hook_tick_delay;
static uint64_t max_hook_second_delay;
static uint64_t max_hook_run_time[ARRAY_SIZE(hook_list)];

static uint64_t avg_hook_tick_delay;
static uint64_t avg_hook_second_delay;
static uint64_t avg_hook_run_time[ARRAY_SIZE(hook_list)];

static inline void update_hook_average(uint64_t *avg, uint64_t time)
{
	*avg = (*avg * 7 + time) >> 3;
}

static void record_hook_delay(uint64_t now, uint64_t last, uint64_t interval,
			      uint64_t *max_delay, uint64_t *avg_delay)
{
	uint64_t delayed = now - last - interval;
	/* Ignore the first call */
	if (last == -interval)
		return;

	if (delayed > *max_delay)
		*max_delay = delayed;
	update_hook_average(avg_delay, delayed);

	/* Warn if delayed by more than 10% */
	if (delayed * 10 > interval)
		CPRINTS("Hook at interval %d us delayed by %d us",
			(uint32_t)interval, (uint32_t)delayed);
}
#endif

void hook_notify(enum hook_type type)
{
	const struct hook_data *start, *end, *p;
	int count, called = 0;
	int last_prio = HOOK_PRIO_FIRST - 1, prio;
#ifdef CONFIG_HOOK_DEBUG
	uint64_t start_time = get_time().val;
	uint64_t run_time;
#endif

	CPRINTS("hook notify %d", type);

	start = hook_list[type].start;
	end = hook_list[type].end;
	count = end - start;

	/* Call all the hooks in priority order */
	while (called < count) {
		/* Find the lowest remaining priority */
		for (p = start, prio = HOOK_PRIO_LAST + 1; p < end; p++) {
			if (p->priority < prio && p->priority > last_prio)
				prio = p->priority;
		}
		last_prio = prio;

		/* Call all the hooks with that priority */
		for (p = start; p < end; p++) {
			if (p->priority == prio) {
				called++;
				p->routine();
			}
		}
	}

#ifdef CONFIG_HOOK_DEBUG
	run_time = get_time().val - start_time;
	if (run_time > max_hook_run_time[type])
		max_hook_run_time[type] = run_time;
	update_hook_average(avg_hook_run_time + type, run_time);
#endif
}

int hook_call_deferred(const struct deferred_data *data, int us)
{
	int i = data - __deferred_funcs;

	if (data < __deferred_funcs || data >= __deferred_funcs_end)
		return EC_ERROR_INVAL; /* Routine not registered */

	if (us == -1) {
		/* Cancel */
		__deferred_until[i] = 0;
	} else {
		/* Set alarm */
		__deferred_until[i] = get_time().val + us;

		/* Wake task so it can re-sleep for the proper time */
		if (hook_task_started)
			task_wake(TASK_ID_HOOKS);
	}

	return EC_SUCCESS;
}

void hook_task(void *u)
{
	/* Periodic hooks will be called first time through the loop */
	static uint64_t last_second = -SECOND;
	static uint64_t last_tick = -HOOK_TICK_INTERVAL;

	hook_task_started = 1;

	/* Call HOOK_INIT hooks. */
	hook_notify(HOOK_INIT);

	/* Now, enable the rest of the tasks. */
	task_enable_all_tasks();

	while (1) {
		uint64_t t = get_time().val;
		int next = 0;
		int i;

		interrupt_disable();
		/* Handle deferred routines */
		for (i = 0; i < DEFERRED_FUNCS_COUNT; i++) {
			if (__deferred_until[i] && __deferred_until[i] < t) {
				/*
				 * Call deferred function.  Clear timer first,
				 * so it can request itself be called later.
				 */
				__deferred_until[i] = 0;
				interrupt_enable();
				CPRINTS("hook call deferred 0x%p",
					__deferred_funcs[i].routine);
				__deferred_funcs[i].routine();
				interrupt_disable();
			}
		}

		interrupt_enable();
		if (t - last_tick >= HOOK_TICK_INTERVAL) {
#ifdef CONFIG_HOOK_DEBUG
			record_hook_delay(t, last_tick, HOOK_TICK_INTERVAL,
					  &max_hook_tick_delay,
					  &avg_hook_tick_delay);
#endif
			hook_notify(HOOK_TICK);
			last_tick = t;
		}

		if (t - last_second >= SECOND) {
#ifdef CONFIG_HOOK_DEBUG
			record_hook_delay(t, last_second, SECOND,
					  &max_hook_second_delay,
					  &avg_hook_second_delay);
#endif
			hook_notify(HOOK_SECOND);
			last_second = t;
		}

		/* Calculate when next tick needs to occur */
		t = get_time().val;
		if (last_tick + HOOK_TICK_INTERVAL > t)
			next = last_tick + HOOK_TICK_INTERVAL - t;

		interrupt_disable();
		for (i = 0; i < DEFERRED_FUNCS_COUNT && next > 0; i++) {
			if (!__deferred_until[i])
				continue;

			if (__deferred_until[i] < t)
				next = 0;
			else if (__deferred_until[i] - t < next)
				next = __deferred_until[i] - t;
		}

		interrupt_enable();

		/*
		 * If nothing is immediately pending, sleep until the next
		 * event.
		 */
		if (next > 0)
			task_wait_event(next);
	}
}

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_HOOK_DEBUG
static void print_hook_delay(uint32_t interval, uint32_t delay, uint32_t avg)
{
	int percentage = delay * 100 / interval;
	int percent_avg = avg * 100 / interval;

	ccprintf("  Interval:    %7d us\n", interval);
	ccprintf("  Max delayed: %7d us (%d%%)\n\n", delay, percentage);
	ccprintf("  Average:     %7d us (%d%%)\n\n", avg, percent_avg);
}

static int command_stats(int argc, const char **argv)
{
	int i;

	ccprintf("HOOK_TICK:\n");
	print_hook_delay(HOOK_TICK_INTERVAL, max_hook_tick_delay,
			 avg_hook_tick_delay);

	ccprintf("HOOK_SECOND:\n");
	print_hook_delay(SECOND, max_hook_second_delay, avg_hook_second_delay);

	ccprintf("Max run time for each hook:\n");
	for (i = 0; i < ARRAY_SIZE(hook_list); ++i)
		ccprintf("%3d:%6d us (Avg: %5d us)\n", i,
			 (uint32_t)max_hook_run_time[i],
			 (uint32_t)avg_hook_run_time[i]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hookstats, command_stats, NULL, "Print stats of hooks");
#endif
