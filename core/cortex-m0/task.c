/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include "atomic.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
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

extern int __task_start(int *task_stack_ready);

#ifndef CONFIG_LOW_POWER_IDLE
/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	while (1) {
		/*
		 * Wait for the next irq event.  This stops the CPU clock
		 * (sleep / deep sleep, depending on chip config).
		 */
		cpu_enter_suspend_mode();
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
/* Validity checks about static task invariants */
BUILD_ASSERT(TASK_ID_COUNT <= sizeof(unsigned) * 8);
BUILD_ASSERT(TASK_ID_COUNT < (1 << (sizeof(task_id_t) * 8)));

/* Stacks for all tasks */
#define TASK(n, r, d, s) +s
uint8_t task_stacks[0 TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE)
			    CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST
				    CONFIG_CTS_TASK_LIST] __aligned(8);

#undef TASK

/* Reserve space to discard context on first context switch. */
uint32_t scratchpad[17];

task_ *current_task = (task_ *)scratchpad;

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
	asm("cpsid i");
}

void interrupt_enable(void)
{
	asm("cpsie i");
}

inline bool is_interrupt_enabled(void)
{
	int primask;

	/* Interrupts are enabled when PRIMASK bit is 0 */
	asm("mrs %0, primask" : "=r"(primask));

	return !(primask & 0x1);
}

inline bool in_interrupt_context(void)
{
	int ret;
	asm("mrs %0, ipsr\n" /* read exception number */
	    : "=r"(ret));
	return ret & GENMASK(8, 0); /* exception bits are the 9 LSB */
}

#ifdef CONFIG_TASK_PROFILING
static inline int get_interrupt_context(void)
{
	int ret;
	asm("mrs %0, ipsr\n" : "=r"(ret)); /* read exception number */
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
task_ __attribute__((noinline)) * __svc_handler(int desched, task_id_t resched)
{
	task_ *current, *next;
#ifdef CONFIG_TASK_PROFILING
	int exc = get_interrupt_context();
	uint32_t t;
#endif

	/* Priority is already at 0 we cannot be interrupted */

#ifdef CONFIG_TASK_PROFILING
	/*
	 * SVCall isn't triggered via DECLARE_IRQ(), so it needs to track its
	 * start time explicitly.
	 */
	if (exc == 0xb) {
		t = get_time().le.lo;
		current_task->runtime += (t - exc_end_time);
		exc_end_time = t;
		svc_calls++;
	}
#endif

	current = current_task;

#ifdef CONFIG_DEBUG_STACK_OVERFLOW
	if (*current->stack != STACK_UNUSED_VALUE) {
		panic_printf("\n\nStack overflow in %s task!\n",
			     task_names[current - tasks]);
#ifdef CONFIG_SOFTWARE_PANIC
		software_panic(PANIC_SW_STACK_OVERFLOW, current - tasks);
#endif
	}
#endif

	if (desched && !current->events) {
		/*
		 * Remove our own ready bit (current - tasks is same as
		 * task_get_current())
		 */
		tasks_ready &= ~(1 << (current - tasks));
	}
	tasks_ready |= 1 << resched;

	ASSERT(tasks_ready & tasks_enabled);
	next = __task_id_to_ptr(__fls(tasks_ready & tasks_enabled));

#ifdef CONFIG_TASK_PROFILING
	/* Track additional time in re-sched exception context */
	t = get_time().le.lo;
	exc_total_time += (t - exc_end_time);

	exc_end_time = t;
#endif

	/* Switch to new task */
#ifdef CONFIG_TASK_PROFILING
	if (next != current)
		task_switches++;
#endif
	current_task = next;
	return current;
}

void __schedule(int desched, int resched)
{
	register int p0 asm("r0") = desched;
	register int p1 asm("r1") = resched;

	asm("svc 0" : : "r"(p0), "r"(p1));
}

#ifdef CONFIG_TASK_PROFILING
void task_start_irq_handler(void *excep_return)
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
	 * Continue iff the tasks are ready and we are not called from another
	 * exception (as the time accouting is done in the outer irq).
	 */
	if (!start_called || (((uint32_t)excep_return & EXC_RETURN_MODE_MASK) ==
			      EXC_RETURN_MODE_HANDLER))
		return;

	exc_start_time = t;
	/*
	 * Bill the current task for time between the end of the last interrupt
	 * and the start of this one.
	 */
	current_task->runtime += (exc_start_time - exc_end_time);
}

void task_end_irq_handler(void *excep_return)
{
	uint32_t t = get_time().le.lo;
	/*
	 * Continue iff the tasks are ready and we are not called from another
	 * exception (as the time accouting is done in the outer irq).
	 */
	if (!start_called || (((uint32_t)excep_return & EXC_RETURN_MODE_MASK) ==
			      EXC_RETURN_MODE_HANDLER))
		return;

	/* Track time in interrupts */
	exc_total_time += (t - exc_start_time);
	exc_end_time = t;
}
#endif

static uint32_t __wait_evt(int timeout_us, task_id_t resched)
{
	task_ *tsk = current_task;
	task_id_t me = tsk - tasks;
	uint32_t evt;
	int ret __attribute__((unused));

	/*
	 * Scheduling task when interrupts are disabled will result in Forced
	 * Hard Fault because disabling interrupt using 'cpsid i' also disables
	 * SVCall handler (because it has configurable priority)
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
		/*
		 * We need to ensure that the execution priority is actually
		 * decreased after the "cpsie i" in the atomic operation above
		 * else the "svc" in the __schedule call below will trigger
		 * a HardFault. Use a barrier to force it at that point.
		 */
		asm volatile("isb");
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
		if (start_called) {
			/*
			 * Trigger the scheduler when there's
			 * no other irqs happening.
			 */
			CPU_SCB_ICSR = BIT(28);
		}
	} else {
		/*
		 * We need to ensure that the execution priority is
		 * actually decreased after the "cpsie i" in the atomic
		 * operation above else the "svc" in the __schedule
		 * call below will trigger a HardFault.
		 * Use a barrier to force it at that point.
		 */
		asm volatile("isb");
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

void task_disable_task(task_id_t tskid)
{
	atomic_clear_bits(&tasks_enabled, BIT(tskid));

	if (!in_interrupt_context() && is_interrupt_enabled() &&
	    tskid == task_get_current())
		__schedule(0, 0);
}

void task_enable_irq(int irq)
{
	CPU_NVIC_EN(0) = 1 << irq;
}

void task_disable_irq(int irq)
{
	CPU_NVIC_DIS(0) = 1 << irq;
}

void task_clear_pending_irq(int irq)
{
	CPU_NVIC_UNPEND(0) = 1 << irq;
}

void task_trigger_irq(int irq)
{
	CPU_NVIC_ISPR(0) = 1 << irq;
}

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
	CPU_NVIC_DIS(0) = 0xffffffff;
	CPU_NVIC_UNPEND(0) = 0xffffffff;

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
	uint32_t id = 1 << task_get_current();

	ASSERT(id != TASK_ID_INVALID);
	atomic_or(&mtx->waiters, id);

	while (1) {
		/* Try to get the lock (set 2 into the lock field) */
		__asm__ __volatile__("cpsid i");
		if (mtx->lock == 0)
			break;
		__asm__ __volatile__("cpsie i");
		/* Contention on the mutex */
		task_wait_event_mask(TASK_EVENT_MUTEX, 0);
	}
	mtx->lock = 2;
	__asm__ __volatile__("cpsie i");

	atomic_clear_bits(&mtx->waiters, id);
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
DECLARE_CONSOLE_COMMAND(taskinfo, command_task_info, NULL, "Print task info");

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
		uint32_t *sp;
		/* Stack size in words */
		uint32_t ssize = tasks_init[i].stack_size / 4;

		tasks[i].stack = stack_next;

		/*
		 * Update stack used by first frame: 8 words for the normal
		 * stack, plus 8 for R4-R11. With FP enabled, we need another
		 * 18 words for S0-S15 and FPCSR and to align to 64-bit.
		 */
		sp = stack_next + ssize - 16;
		tasks[i].sp = (uint32_t)sp;

		/* Initial context on stack (see __switchto()) */
		sp[8] = tasks_init[i].r0; /* r0 */
		sp[13] = (uint32_t)task_exit_trap; /* lr */
		sp[14] = tasks_init[i].pc; /* pc */
		sp[15] = 0x01000000; /* psr */

		/* Fill unused stack; also used to detect stack overflow. */
		for (sp = stack_next; sp < (uint32_t *)tasks[i].sp; sp++)
			*sp = STACK_UNUSED_VALUE;

		stack_next += ssize;
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

int task_start(void)
{
#ifdef CONFIG_TASK_PROFILING
	timestamp_t t = get_time();

	task_start_time = t.val;
	exc_end_time = t.le.lo;
#endif

	return __task_start(&start_called);
}
