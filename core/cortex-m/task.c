/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include "atomic.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "debug.h"
#include "link_defs.h"
#include "panic.h"
#include "task.h"
#include "timer.h"
#include "util.h"

typedef union {
	struct {
		/*
		 * Note that sp must be the first element in the task struct
		 * for __switchto() to work.
		 */
		uint32_t sp; /* Saved stack pointer for context switch */
		atomic_t events; /* Bitmaps of received events */
		uint64_t runtime; /* Time spent in task */
		uint32_t *stack; /* Start of stack */
	};
} task_;

/* Value to store in unused stack */
#define STACK_UNUSED_VALUE 0xdeadd00d

/* declare task routine prototypes */
#define TASK(n, r, d, s) void r(void *);
void __idle(void);
CONFIG_TASK_LIST
CONFIG_TEST_TASK_LIST
CONFIG_CTS_TASK_LIST
#undef TASK

/* Task names for easier debugging */
#define TASK(n, r, d, s) #n,
static const char *const task_names[] = {
	"<< idle >>",
	CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST CONFIG_CTS_TASK_LIST
};
#undef TASK

#ifdef CONFIG_TASK_PROFILING
static uint64_t task_start_time; /* Time task scheduling started */
/*
 * We only keep 32-bit values for exception start/end time, to avoid
 * accounting errors when we service interrupt when the timer wraps around.
 */
static uint32_t exc_start_time; /* Time of task->exception transition */
static uint32_t exc_end_time; /* Time of exception->task transition */
static uint64_t exc_total_time; /* Total time in exceptions */
static uint32_t svc_calls; /* Number of service calls */
static uint32_t task_switches; /* Number of times active task changed */
static uint32_t irq_dist[CONFIG_IRQ_COUNT]; /* Distribution of IRQ calls */
#endif

extern void __switchto(task_ *from, task_ *to);
extern int __task_start(int *task_stack_ready);

#ifndef CONFIG_LOW_POWER_IDLE
/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	while (1) {
#ifdef CHIP_NPCX

		/*
		 * Using host access to make sure M4 core clock will
		 * return when the eSPI accesses the Host modules if
		 * CSAE bit is set. Please notice this symptom only
		 * occurs at npcx5.
		 */
#if defined(CHIP_FAMILY_NPCX5) && defined(CONFIG_HOST_INTERFACE_ESPI)
		/* Enable Host access wakeup */
		SET_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 6);
#endif

		/*
		 * TODO (ML): A interrupt that occurs shortly before entering
		 * idle mode starts getting services while the Core transitions
		 * into idle mode. The results in a hard fault when the Core,
		 * shortly therefore, resumes execution on exiting idle mode.
		 * Workaround: Replace the idle function with the followings
		 */
		asm("cpsid i\n" /* Disable interrupt */
		    "push {r0-r5}\n" /* Save needed registers */
		    "wfi\n" /* Wait for int to enter idle */
		    "ldm %0, {r0-r5}\n" /* Add a delay after WFI */
		    "pop {r0-r5}\n" /* Restore regs before enabling ints */
		    "isb\n" /* Flush the cpu pipeline */
		    "cpsie i\n" ::"r"(0x100A8000) /* Enable interrupts */
		);
#else
		/*
		 * Wait for the next irq event.  This stops the CPU clock
		 * (sleep / deep sleep, depending on chip config).
		 */
		cpu_enter_suspend_mode();
#endif
	}
}
#endif /* !CONFIG_LOW_POWER_IDLE */

static void task_exit_trap(void)
{
	int i = task_get_current();
	cprints(CC_TASK, "Task %d (%s) exited!", i, task_names[i]);
	/* Exited tasks simply sleep forever */
	while (1)
		task_wait_event(-1);
}

/* Startup parameters for all tasks. */
#define TASK(n, r, d, s)           \
	{                          \
		.r0 = (uint32_t)d, \
		.pc = (uint32_t)r, \
		.stack_size = s,   \
	},
static const struct {
	uint32_t r0;
	uint32_t pc;
	uint16_t stack_size;
} tasks_init[] = {
	TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE)
		CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST CONFIG_CTS_TASK_LIST
};
#undef TASK

/* Contexts for all tasks */
static task_ tasks[TASK_ID_COUNT];

/* Reset constants and state for all tasks */
#define TASK_RESET_SUPPORTED BIT(31)
#define TASK_RESET_LOCK BIT(30)
#define TASK_RESET_STATE_MASK (TASK_RESET_SUPPORTED | TASK_RESET_LOCK)
#define TASK_RESET_WAITERS_MASK ~TASK_RESET_STATE_MASK
#define TASK_RESET_UNSUPPORTED 0
#define TASK_RESET_STATE_LOCKED (TASK_RESET_SUPPORTED | TASK_RESET_LOCK)
#define TASK_RESET_STATE_UNLOCKED TASK_RESET_SUPPORTED

#ifdef CONFIG_TASK_RESET_LIST
#define ENABLE_RESET(n) [TASK_ID_##n] = TASK_RESET_SUPPORTED,
static uint32_t task_reset_state[TASK_ID_COUNT] = {
#ifdef CONFIG_TASK_RESET_LIST
	CONFIG_TASK_RESET_LIST
#endif
};
#undef ENABLE_RESET
#endif /* CONFIG_TASK_RESET_LIST */

/* Validity checks about static task invariants */
BUILD_ASSERT(TASK_ID_COUNT <= sizeof(unsigned int) * 8);
BUILD_ASSERT(TASK_ID_COUNT < (1 << (sizeof(task_id_t) * 8)));
BUILD_ASSERT(BIT(TASK_ID_COUNT) < TASK_RESET_LOCK);

/* Stacks for all tasks */
#define TASK(n, r, d, s) +s
uint8_t task_stacks[0 TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE)
			    CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST
				    CONFIG_CTS_TASK_LIST] __aligned(8);

#undef TASK

/* Reserve space to discard context on first context switch. */
uint32_t scratchpad[17];

static task_ *current_task = (task_ *)scratchpad;

/*
 * Should IRQs chain to svc_handler()?  This should be set if either of the
 * following is true:
 *
 * 1) Task scheduling has started, and task profiling is enabled.  Task
 * profiling does its tracking in svc_handler().
 *
 * 2) An event was set by an interrupt; this could result in a higher-priority
 * task unblocking.  After checking for a task switch, svc_handler() will clear
 * the flag (unless profiling is also enabled; then the flag remains set).
 */
static int need_resched_or_profiling;

/*
 * Bitmap of all tasks ready to be run.
 *
 * Start off with only the hooks task marked as ready such that all the modules
 * can do their init within a task switching context.  The hooks task will then
 * make a call to enable all tasks.
 */
static atomic_t tasks_ready = BIT(TASK_ID_HOOKS);
/*
 * Initially allow only the HOOKS and IDLE task to run, regardless of ready
 * status, in order for HOOK_INIT to complete before other tasks.
 * task_enable_all_tasks() will open the flood gates.
 */
static atomic_t tasks_enabled = BIT(TASK_ID_HOOKS) | BIT(TASK_ID_IDLE);

static int start_called; /* Has task swapping started */

static inline task_ *__task_id_to_ptr(task_id_t id)
{
	return tasks + id;
}

void interrupt_disable(void)
{
	asm volatile("cpsid i");
}

void interrupt_enable(void)
{
	asm volatile("cpsie i");
}

inline bool is_interrupt_enabled(void)
{
	int primask;

	/* Interrupts are enabled when PRIMASK bit is 0 */
	asm volatile("mrs %0, primask" : "=r"(primask));

	return !(primask & 0x1);
}

inline bool in_interrupt_context(void)
{
	int ret;
	asm volatile("mrs %0, ipsr \n" /* read exception number */
		     "lsl %0, #23  \n"
		     : "=r"(ret)); /* exception bits are the 9 LSB */
	return ret;
}

#ifdef CONFIG_TASK_PROFILING
static inline int get_interrupt_context(void)
{
	int ret;
	asm("mrs %0, ipsr \n" : "=r"(ret)); /* read exception number */
	return ret & 0x1ff; /* exception bits are the 9 LSB */
}
#endif

task_id_t task_get_current(void)
{
#ifdef CONFIG_DEBUG_BRINGUP
	/* If we haven't done a context switch then our task ID isn't valid */
	ASSERT(current_task != (task_ *)scratchpad);
#endif
	return current_task - tasks;
}

atomic_t *task_get_event_bitmap(task_id_t tskid)
{
	task_ *tsk = __task_id_to_ptr(tskid);
	return &tsk->events;
}

int task_start_called(void)
{
	return start_called;
}

/**
 * Scheduling system call
 */
void svc_handler(int desched, task_id_t resched)
{
	task_ *current, *next;
#ifdef CONFIG_TASK_PROFILING
	int exc = get_interrupt_context();
	uint32_t t;
#endif

	/*
	 * Push the priority to -1 until the return, to avoid being
	 * interrupted.
	 */
	asm volatile("cpsid f\n"
		     "isb\n");

#ifdef CONFIG_TASK_PROFILING
	/*
	 * SVCall isn't triggered via DECLARE_IRQ(), so it needs to track its
	 * start time explicitly.
	 */
	if (exc == 0xb) {
		exc_start_time = get_time().le.lo;
		svc_calls++;
	}
#endif

	current = current_task;

#ifdef CONFIG_DEBUG_STACK_OVERFLOW
	if (*current->stack != STACK_UNUSED_VALUE &&
	    task_enabled(current - tasks)) {
		panic_printf("\n\nStack overflow in %s task!\n",
			     task_names[current - tasks]);
		software_panic(PANIC_SW_STACK_OVERFLOW, current - tasks);
	}
#endif

	if (desched && !current->events) {
		/*
		 * Remove our own ready bit (current - tasks is same as
		 * task_get_current())
		 */
		tasks_ready &= ~(1 << (current - tasks));
	}
	ASSERT(resched <= TASK_ID_COUNT);
	tasks_ready |= 1 << resched;

	ASSERT(tasks_ready & tasks_enabled);
	next = __task_id_to_ptr(__fls(tasks_ready & tasks_enabled));

#ifdef CONFIG_TASK_PROFILING
	/* Track time in interrupts */
	t = get_time().le.lo;
	exc_total_time += (t - exc_start_time);

	/*
	 * Bill the current task for time between the end of the last interrupt
	 * and the start of this one.
	 */
	current->runtime += (exc_start_time - exc_end_time);
	exc_end_time = t;
#else
	/*
	 * Don't chain here from interrupts until the next time an interrupt
	 * sets an event.
	 */
	need_resched_or_profiling = 0;
#endif

	/* Nothing to do */
	if (next == current)
		return;

		/* Switch to new task */
#ifdef CONFIG_TASK_PROFILING
	task_switches++;
#endif
	current_task = next;
	__switchto(current, next);
}

void __schedule(int desched, int resched)
{
	register int p0 asm("r0") = desched;
	register int p1 asm("r1") = resched;

	asm("svc 0" ::"r"(p0), "r"(p1));
}

#ifdef CONFIG_TASK_PROFILING
void __keep task_start_irq_handler(void *excep_return)
{
	/*
	 * Get time before checking depth, in case this handler is
	 * pre-empted.
	 */
	uint32_t t = get_time().le.lo;
	int irq = get_interrupt_context() - 16;

	/*
	 * Track IRQ distribution.  No need for atomic add, because an IRQ
	 * can't pre-empt itself.
	 */
	if (irq < ARRAY_SIZE(irq_dist))
		irq_dist[irq]++;

	/*
	 * Continue iff a rescheduling event happened or profiling is active,
	 * and we are not called from another exception (this must match the
	 * logic for when we chain to svc_handler() below).
	 */
	if (!need_resched_or_profiling ||
	    (((uint32_t)excep_return & EXC_RETURN_MODE_MASK) ==
	     EXC_RETURN_MODE_HANDLER))
		return;

	exc_start_time = t;
}
#endif

void __keep task_resched_if_needed(void *excep_return)
{
	/*
	 * Continue iff a rescheduling event happened or profiling is active,
	 * and we are not called from another exception.
	 */
	if (!need_resched_or_profiling ||
	    (((uint32_t)excep_return & EXC_RETURN_MODE_MASK) ==
	     EXC_RETURN_MODE_HANDLER))
		return;

	svc_handler(0, 0);
}

static uint32_t __wait_evt(int timeout_us, task_id_t resched)
{
	task_ *tsk = current_task;
	task_id_t me = tsk - tasks;
	uint32_t evt;
	int ret __attribute__((unused));

	/*
	 * Scheduling task when interrupts are disabled will result in Forced
	 * Hard Fault because:
	 * - Disabling interrupt using 'cpsid i' also disables SVCall handler
	 *   (because it has configurable priority)
	 * - Escalation to Hard Fault (also known as 'priority escalation')
	 *   occurs when handler for that fault is not enabled
	 */
	ASSERT(is_interrupt_enabled());
	ASSERT(!in_interrupt_context());

	if (timeout_us > 0) {
		timestamp_t deadline = get_time();
		deadline.val += timeout_us;
		ret = timer_arm(deadline, me);
		ASSERT(ret == EC_SUCCESS);
	}
	while (!(evt = atomic_clear(&tsk->events))) {
		/* Remove ourself and get the next task in the scheduler */
		__schedule(1, resched);
		resched = TASK_ID_IDLE;
	}
	if (timeout_us > 0) {
		timer_cancel(me);
		/* Ensure timer event is clear, we no longer care about it */
		atomic_clear_bits(&tsk->events, TASK_EVENT_TIMER);
	}
	return evt;
}

void task_set_event(task_id_t tskid, uint32_t event)
{
	task_ *receiver = __task_id_to_ptr(tskid);
	ASSERT(receiver);

	/* Set the event bit in the receiver message bitmap */
	atomic_or(&receiver->events, event);

	/* Re-schedule if priorities have changed */
	if (in_interrupt_context() || !is_interrupt_enabled()) {
		/* The receiver might run again */
		atomic_or(&tasks_ready, 1 << tskid);
#ifndef CONFIG_TASK_PROFILING
		if (start_called)
			need_resched_or_profiling = 1;
#endif
	} else {
		__schedule(0, tskid);
	}
}

uint32_t task_wait_event(int timeout_us)
{
	return __wait_evt(timeout_us, TASK_ID_IDLE);
}

uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us)
{
	uint64_t deadline = get_time().val + timeout_us;
	uint32_t events = 0;
	int time_remaining_us = timeout_us;

	/* Add the timer event to the mask so we can indicate a timeout */
	event_mask |= TASK_EVENT_TIMER;

	while (!(events & event_mask)) {
		/* Collect events to re-post later */
		events |= __wait_evt(time_remaining_us, TASK_ID_IDLE);

		time_remaining_us = deadline - get_time().val;
		if (timeout_us > 0 && time_remaining_us <= 0) {
			/* Ensure we return a TIMER event if we timeout */
			events |= TASK_EVENT_TIMER;
			break;
		}
	}

	/* Re-post any other events collected */
	if (events & ~event_mask)
		atomic_or(&current_task->events, events & ~event_mask);

	return events & event_mask;
}

void task_enable_all_tasks(void)
{
	/* Mark all tasks as ready and able to run. */
	tasks_ready = tasks_enabled = BIT(TASK_ID_COUNT) - 1;
	/* Reschedule the highest priority task. */
	if (is_interrupt_enabled())
		__schedule(0, 0);
}

void task_enable_task(task_id_t tskid)
{
	atomic_or(&tasks_enabled, BIT(tskid));
}

bool task_enabled(task_id_t tskid)
{
	return tasks_enabled & BIT(tskid);
}

void task_disable_task(task_id_t tskid)
{
	atomic_clear_bits(&tasks_enabled, BIT(tskid));

	if (!in_interrupt_context() && is_interrupt_enabled() &&
	    tskid == task_get_current())
		__schedule(0, 0);
}

void task_enable_irq(int irq)
{
	CPU_NVIC_EN(irq / 32) = 1 << (irq % 32);
}

void __keep task_disable_irq(int irq)
{
	CPU_NVIC_DIS(irq / 32) = 1 << (irq % 32);
}

void task_clear_pending_irq(int irq)
{
	CPU_NVIC_UNPEND(irq / 32) = 1 << (irq % 32);
}

/*
 * Reading interrupt clear-pending register gives us information if interrupt
 * is pending.
 */
bool task_is_irq_pending(int irq)
{
	return CPU_NVIC_UNPEND(irq / 32) & (1 << (irq % 32));
}

void task_trigger_irq(int irq)
{
	CPU_NVIC_SWTRIG = irq;
}

static uint32_t init_task_context(task_id_t id)
{
	uint32_t *sp;
	/* Stack size in words */
	uint32_t ssize = tasks_init[id].stack_size / 4;

	/*
	 * Update stack used by first frame: 8 words for the normal
	 * stack, plus 8 for R4-R11. Even if using FPU, the first frame
	 * does not store FP regs.
	 */
	sp = tasks[id].stack + ssize - 16;
	tasks[id].sp = (uint32_t)sp;

	/* Initial context on stack (see __switchto()) */
	sp[8] = tasks_init[id].r0; /* r0 */
	sp[13] = (uint32_t)task_exit_trap; /* lr */
	sp[14] = tasks_init[id].pc; /* pc */
	sp[15] = 0x01000000; /* psr */

	/* Fill unused stack; also used to detect stack overflow. */
	for (sp = tasks[id].stack; sp < (uint32_t *)tasks[id].sp; sp++)
		*sp = STACK_UNUSED_VALUE;

	return ssize;
}

#ifdef CONFIG_TASK_RESET_LIST

/*
 * Re-initializes a task stack to its initial state, and marks it ready.
 * The task reset lock must be held prior to calling this function.
 */
static void do_task_reset(task_id_t id)
{
	interrupt_disable();
	init_task_context(id);
	tasks_ready |= 1 << id;
	/* TODO: Clear all pending events? */
	interrupt_enable();
}

/* We can't pass a parameter to a deferred call. Use this instead. */
/* Mask of task IDs waiting to be reset. */
static uint32_t deferred_reset_task_ids;

/* Tasks may call this function if they want to reset themselves. */
static void deferred_task_reset(void)
{
	while (deferred_reset_task_ids) {
		task_id_t reset_id = __fls(deferred_reset_task_ids);

		atomic_clear_bits(&deferred_reset_task_ids, 1 << reset_id);
		do_task_reset(reset_id);
	}
}
DECLARE_DEFERRED(deferred_task_reset);

/*
 * Helper for updating task_reset state atomically. Checks the current state,
 * and if it matches if_value, updates the state to new_value, and returns
 * TRUE.
 */
static int update_reset_state(uint32_t *state, uint32_t if_value,
			      uint32_t to_value)
{
	int update;

	interrupt_disable();
	update = *state == if_value;
	if (update)
		*state = to_value;
	interrupt_enable();

	return update;
}

/*
 * Helper that acquires the reset lock iff it is not currently held.
 * Returns TRUE if the lock was acquired.
 */
static inline int try_acquire_reset_lock(uint32_t *state)
{
	return update_reset_state(state,
				  /* if the lock is not held */
				  TASK_RESET_STATE_UNLOCKED,
				  /* acquire it */
				  TASK_RESET_STATE_LOCKED);
}

/*
 * Helper that releases the reset lock iff it is currently held, and there
 * are no pending resets. Returns TRUE if the lock was released.
 */
static inline int try_release_reset_lock(uint32_t *state)
{
	return update_reset_state(state,
				  /* if the lock is held, with no waiters */
				  TASK_RESET_STATE_LOCKED,
				  /* release it */
				  TASK_RESET_STATE_UNLOCKED);
}

/*
 * Helper to cause the current task to sleep indefinitely; useful if the
 * calling task just needs to block until it is reset.
 */
static inline void sleep_forever(void)
{
	while (1)
		usleep(-1);
}

void task_enable_resets(void)
{
	task_id_t id = task_get_current();
	uint32_t *state = &task_reset_state[id];

	if (*state == TASK_RESET_UNSUPPORTED) {
		cprints(CC_TASK, "%s called from non-resettable task, id: %d",
			__func__, id);
		return;
	}

	/*
	 * A correctly written resettable task will only call this function
	 * if resets are currently disabled; this implies that this task
	 * holds the reset lock.
	 */

	if (*state == TASK_RESET_STATE_UNLOCKED) {
		cprints(CC_TASK,
			"%s called, but resets already enabled, id: %d",
			__func__, id);
		return;
	}

	/*
	 * Attempt to release the lock. If we cannot, it means there are tasks
	 * waiting for a reset.
	 */
	if (try_release_reset_lock(state))
		return;

	/* People are waiting for us to reset; schedule a reset. */
	atomic_or(&deferred_reset_task_ids, 1 << id);
	/*
	 * This will always trigger a deferred call after our new ID was
	 * written. If the hook call is currently executing, it will run
	 * again.
	 */
	hook_call_deferred(&deferred_task_reset_data, 0);
	/* Wait to be reset. */
	sleep_forever();
}

void task_disable_resets(void)
{
	task_id_t id = task_get_current();
	uint32_t *state = &task_reset_state[id];

	if (*state == TASK_RESET_UNSUPPORTED) {
		cprints(CC_TASK, "%s called from non-resettable task, id %d",
			__func__, id);
		return;
	}

	/*
	 * A correctly written resettable task will only call this function
	 * if resets are currently enabled; this implies that this task does
	 * not hold the reset lock.
	 */

	if (try_acquire_reset_lock(state))
		return;

	/*
	 * If we can't acquire the lock, we are about to be reset by another
	 * task.
	 */
	sleep_forever();
}

int task_reset_cleanup(void)
{
	task_id_t id = task_get_current();
	uint32_t *state = &task_reset_state[id];

	/*
	 * If the task has never started before, state will be
	 * TASK_RESET_ENABLED.
	 *
	 * If the task was reset, the TASK_RESET_LOCK bit will be set, and
	 * there may additionally be bits representing tasks we must notify
	 * that we have reset.
	 */

	/*
	 * Only this task can unset the lock bit so we can read this safely,
	 * even though other tasks may be modifying the state to add themselves
	 * as waiters.
	 */
	int cleanup_req = *state & TASK_RESET_LOCK;

	/*
	 * Attempt to release the lock. We can only do this when there are no
	 * tasks waiting to be notified that we have been reset, so we loop
	 * until no tasks are waiting.
	 *
	 * Other tasks may still be trying to reset us at this point; if they
	 * do, they will add themselves to the list of tasks we must notify. We
	 * will simply notify them (multiple times if necessary) until we are
	 * free to unlock.
	 */
	if (cleanup_req) {
		while (!try_release_reset_lock(state)) {
			/* Find the first waiter to notify. */
			task_id_t notify_id =
				__fls(*state & TASK_RESET_WAITERS_MASK);
			/*
			 * Remove the task from waiters first, so that
			 * when it wakes after being notified, it is in
			 * a consistent state (it should not be waiting
			 * to be notified and running).
			 * After being notified, the task may try to
			 * reset us again; if it does, it will just add
			 * itself back to the list of tasks to notify,
			 * and we will notify it again.
			 */
			atomic_clear_bits(state, 1 << notify_id);
			/*
			 * Skip any invalid ids set by tasks that
			 * requested a non-blocking reset.
			 */
			if (notify_id < TASK_ID_COUNT)
				task_set_event(notify_id,
					       TASK_EVENT_RESET_DONE);
		}
	}

	return cleanup_req;
}

int task_reset(task_id_t id, int wait)
{
	task_id_t current = task_get_current();
	uint32_t *state = &task_reset_state[id];
	uint32_t waiter_id;
	int resets_disabled;

	if (id == current)
		return EC_ERROR_INVAL;

	/*
	 * This value is only set at compile time, and will never be modified.
	 */
	if (*state == TASK_RESET_UNSUPPORTED)
		return EC_ERROR_INVAL;

	/*
	 * If we are not blocking for reset, we use an invalid task id to notify
	 * the task that _someone_ wanted it to reset, but didn't want to be
	 * notified when the reset is complete.
	 */
	waiter_id = 1 << (wait ? current : TASK_ID_COUNT);

	/*
	 * Try and take the lock. If we can't have it, just notify the task we
	 * tried; it will reset itself when it next tries to release the lock.
	 */
	interrupt_disable();
	resets_disabled = *state & TASK_RESET_LOCK;
	if (resets_disabled)
		*state |= waiter_id;
	else
		*state |= TASK_RESET_LOCK;
	interrupt_enable();

	if (!resets_disabled) {
		/* We got the lock, do the reset immediately. */
		do_task_reset(id);
	} else if (wait) {
		/*
		 * We couldn't get the lock, and have been asked to block for
		 * reset. We have asked the task to reset itself; it will notify
		 * us when it has.
		 */
		task_wait_event_mask(TASK_EVENT_RESET_DONE, -1);
	}

	return EC_SUCCESS;
}

#endif /* CONFIG_TASK_RESET_LIST */

/*
 * Initialize IRQs in the NVIC and set their priorities as defined by the
 * DECLARE_IRQ statements.
 */
static void __nvic_init_irqs(void)
{
	/* Get the IRQ priorities section from the linker */
	int exc_calls = __irqprio_end - __irqprio;
	int i;

	/* Mask and clear all pending interrupts */
	for (i = 0; i < 5; i++) {
		CPU_NVIC_DIS(i) = 0xffffffff;
		CPU_NVIC_UNPEND(i) = 0xffffffff;
	}

	/*
	 * Re-enable global interrupts in case they're disabled.  On a reboot,
	 * they're already enabled; if we've jumped here from another image,
	 * they're not.
	 */
	interrupt_enable();

	/* Set priorities */
	for (i = 0; i < exc_calls; i++) {
		cpu_set_interrupt_priority(__irqprio[i].irq,
					   __irqprio[i].priority);
	}
}

void mutex_lock(struct mutex *mtx)
{
	uint32_t id;

	/*
	 * mutex_lock() must not be used in interrupt context (because we wait
	 * if there is contention).
	 */
	ASSERT(!in_interrupt_context());

	/*
	 * Task ID is not valid before task_start() (since current_task is
	 * scratchpad), and no need for mutex locking before task switching has
	 * begun.
	 */
	if (!task_start_called())
		return;

	id = 1 << task_get_current();

	atomic_or(&mtx->waiters, id);

	while (!mutex_try_lock(mtx)) {
		/* Contention on the mutex */
		task_wait_event_mask(TASK_EVENT_MUTEX, 0);
	}

	atomic_clear_bits(&mtx->waiters, id);
}

int mutex_try_lock(struct mutex *mtx)
{
	uint32_t value;

	/* mutex_try_lock() must not be used in interrupt context. */
	ASSERT(!in_interrupt_context());

	/*
	 * Task ID is not valid before task_start() (since current_task is
	 * scratchpad), and no need for mutex locking before task switching has
	 * begun.
	 */
	if (!task_start_called())
		return 1;

	/* Try to get the lock (set 1 into the lock field) */
	__asm__ __volatile__("   ldrex   %0, [%1]\n"
			     "   teq     %0, #0\n"
			     "   it eq\n"
			     "   strexeq %0, %2, [%1]\n"
			     : "=&r"(value)
			     : "r"(&mtx->lock), "r"(2)
			     : "cc");
	/*
	 * "value" is equals to 1 if the store conditional failed,
	 * 2 if somebody else owns the mutex, 0 else.
	 */
	if (value == 2) {
		/* Contention on the mutex */
		return 0;
	}

	return 1;
}

void mutex_unlock(struct mutex *mtx)
{
	uint32_t waiters;
	task_ *tsk = current_task;

	/*
	 * Add a critical section to keep the unlock and the snapshotting of
	 * waiters atomic in case a task switching occurs between them.
	 */
	interrupt_disable();
	waiters = mtx->waiters;
	mtx->lock = 0;
	interrupt_enable();

	while (waiters) {
		task_id_t id = __fls(waiters);
		waiters &= ~BIT(id);

		/* Somebody is waiting on the mutex */
		task_set_event(id, TASK_EVENT_MUTEX);
	}

	/* Ensure no event is remaining from mutex wake-up */
	atomic_clear_bits(&tsk->events, TASK_EVENT_MUTEX);
}

void task_print_list(void)
{
	int i;

	ccputs("Task Ready Name         Events      Time (s)  StkUsed\n");

	for (i = 0; i < TASK_ID_COUNT; i++) {
		char is_ready = ((uint32_t)tasks_ready & BIT(i)) ? 'R' : ' ';
		uint32_t *sp;

		int stackused = tasks_init[i].stack_size;

		for (sp = tasks[i].stack;
		     sp < (uint32_t *)tasks[i].sp && *sp == STACK_UNUSED_VALUE;
		     sp++)
			stackused -= sizeof(uint32_t);

		ccprintf("%4d %c %-16s %08x %11.6lld  %3d/%3d\n", i, is_ready,
			 task_names[i], (int)tasks[i].events, tasks[i].runtime,
			 stackused, tasks_init[i].stack_size);
		cflush();
	}
}

static int command_task_info(int argc, const char **argv)
{
#ifdef CONFIG_TASK_PROFILING
	int total = 0;
	int i;
#endif

	task_print_list();

#ifdef CONFIG_TASK_PROFILING
	ccputs("IRQ counts by type:\n");
	cflush();
	for (i = 0; i < ARRAY_SIZE(irq_dist); i++) {
		if (irq_dist[i]) {
			ccprintf("%4d %8d\n", i, irq_dist[i]);
			total += irq_dist[i];
		}
	}
	ccprintf("Service calls:          %11d\n", svc_calls);
	ccprintf("Total exceptions:       %11d\n", total + svc_calls);
	ccprintf("Task switches:          %11d\n", task_switches);
	ccprintf("Task switching started: %11.6lld s\n", task_start_time);
	ccprintf("Time in tasks:          %11.6lld s\n",
		 get_time().val - task_start_time);
	ccprintf("Time in exceptions:     %11.6lld s\n", exc_total_time);
#endif

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(taskinfo, command_task_info, NULL,
			     "Print task info");

#ifdef CONFIG_CMD_TASKREADY
static int command_task_ready(int argc, const char **argv)
{
	if (argc < 2) {
		ccprintf("tasks_ready: 0x%08x\n", (int)tasks_ready);
	} else {
		tasks_ready = strtoi(argv[1], NULL, 16);
		ccprintf("Setting tasks_ready to 0x%08x\n", (int)tasks_ready);
		__schedule(0, 0);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskready, command_task_ready, "[setmask]",
			"Print/set ready tasks");
#endif

void task_pre_init(void)
{
	uint32_t *stack_next = (uint32_t *)task_stacks;
	int i;

	/* Fill the task memory with initial values */
	for (i = 0; i < TASK_ID_COUNT; i++) {
		tasks[i].stack = stack_next;
		stack_next += init_task_context(i);
	}

	/*
	 * Fill in guard value in scratchpad to prevent stack overflow
	 * detection failure on the first context switch.  This works because
	 * the first word in the scratchpad is where the switcher will store
	 * sp, so it's ok to blow away.
	 */
	((task_ *)scratchpad)->stack = (uint32_t *)scratchpad;
	*(uint32_t *)scratchpad = STACK_UNUSED_VALUE;

	/* Initialize IRQs */
	__nvic_init_irqs();
}

void task_clear_fp_used(void)
{
	int ctrl;

	/* Clear the CONTROL.FPCA bit, which represents FP context active. */
	asm volatile("mrs %0, control" : "=r"(ctrl));
	ctrl &= ~0x4;
	asm volatile("msr control, %0" : : "r"(ctrl));

	/* Flush pipeline before returning. */
	asm volatile("isb");
}

int task_start(void)
{
#ifdef CONFIG_TASK_PROFILING
	timestamp_t t = get_time();

	task_start_time = t.val;
	exc_end_time = t.le.lo;
#endif
	start_called = 1;

	return __task_start(&need_resched_or_profiling);
}

#ifdef CONFIG_CMD_TASK_RESET
static int command_task_reset(int argc, const char **argv)
{
	task_id_t id;
	char *e;

	if (argc == 2) {
		id = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
		ccprintf("Resetting task %d\n", id);
		return task_reset(id, 1);
	}

	return EC_ERROR_PARAM_COUNT;
}
DECLARE_CONSOLE_COMMAND(taskreset, command_task_reset, "task_id",
			"Reset a task");
#endif /* CONFIG_CMD_TASK_RESET */
