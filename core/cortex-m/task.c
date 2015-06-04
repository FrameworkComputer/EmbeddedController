/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "link_defs.h"
#include "panic.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

typedef union {
	struct {
		/*
		 * Note that sp must be the first element in the task struct
		 * for __switchto() to work.
		 */
		uint32_t sp;       /* Saved stack pointer for context switch */
		uint32_t events;   /* Bitmaps of received events */
		uint64_t runtime;  /* Time spent in task */
		uint32_t *stack;   /* Start of stack */
	};
} task_;

/* Value to store in unused stack */
#define STACK_UNUSED_VALUE 0xdeadd00d

/* declare task routine prototypes */
#define TASK(n, r, d, s) int r(void *);
void __idle(void);
CONFIG_TASK_LIST
CONFIG_TEST_TASK_LIST
#undef TASK

/* Task names for easier debugging */
#define TASK(n, r, d, s)  #n,
static const char * const task_names[] = {
	"<< idle >>",
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
};
#undef TASK

#ifdef CONFIG_TASK_PROFILING
static uint64_t task_start_time; /* Time task scheduling started */
static uint64_t exc_start_time;  /* Time of task->exception transition */
static uint64_t exc_end_time;    /* Time of exception->task transition */
static uint64_t exc_total_time;  /* Total time in exceptions */
static uint32_t svc_calls;       /* Number of service calls */
static uint32_t task_switches;   /* Number of times active task changed */
static uint32_t irq_dist[CONFIG_IRQ_COUNT];  /* Distribution of IRQ calls */
#endif

extern void __switchto(task_ *from, task_ *to);
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
		asm("wfi");
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
#define TASK(n, r, d, s)  {	\
	.r0 = (uint32_t)d,	\
	.pc = (uint32_t)r,	\
	.stack_size = s,	\
},
static const struct {
	uint32_t r0;
	uint32_t pc;
	uint16_t stack_size;
} const tasks_init[] = {
	TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE)
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
};
#undef TASK

/* Contexts for all tasks */
static task_ tasks[TASK_ID_COUNT];
/* Sanity checks about static task invariants */
BUILD_ASSERT(TASK_ID_COUNT <= sizeof(unsigned) * 8);
BUILD_ASSERT(TASK_ID_COUNT < (1 << (sizeof(task_id_t) * 8)));


/* Stacks for all tasks */
#define TASK(n, r, d, s)  + s
uint8_t task_stacks[0
		    TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE)
		    CONFIG_TASK_LIST
		    CONFIG_TEST_TASK_LIST
] __aligned(8);

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
 * Currently all tasks are enabled at startup.
 */
static uint32_t tasks_ready = (1<<TASK_ID_COUNT) - 1;

static int start_called;  /* Has task swapping started */

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

inline int in_interrupt_context(void)
{
	int ret;
	asm("mrs %0, ipsr \n"             /* read exception number */
	    "lsl %0, #23  \n":"=r"(ret)); /* exception bits are the 9 LSB */
	return ret;
}

inline int get_interrupt_context(void)
{
	int ret;
	asm("mrs %0, ipsr \n":"=r"(ret)); /* read exception number */
	return ret & 0x1ff;               /* exception bits are the 9 LSB */
}

task_id_t task_get_current(void)
{
	return current_task - tasks;
}

uint32_t *task_get_event_bitmap(task_id_t tskid)
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
	uint64_t t;
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
		exc_start_time = get_time().val;
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

	ASSERT(tasks_ready);
	next = __task_id_to_ptr(31 - __builtin_clz(tasks_ready));

#ifdef CONFIG_TASK_PROFILING
	/* Track time in interrupts */
	t = get_time().val;
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

	asm("svc 0"::"r"(p0),"r"(p1));
}

#ifdef CONFIG_TASK_PROFILING
void __keep task_start_irq_handler(void *excep_return)
{
	/*
	 * Get time before checking depth, in case this handler is
	 * pre-empted.
	 */
	uint64_t t = get_time().val;
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
	if (!need_resched_or_profiling || (((uint32_t)excep_return & 0xf) == 1))
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
	if (!need_resched_or_profiling || (((uint32_t)excep_return & 0xf) == 1))
		return;

	svc_handler(0, 0);
}

static uint32_t __wait_evt(int timeout_us, task_id_t resched)
{
	task_ *tsk = current_task;
	task_id_t me = tsk - tasks;
	uint32_t evt;
	int ret __attribute__((unused));

	ASSERT(!in_interrupt_context());

	if (timeout_us > 0) {
		timestamp_t deadline = get_time();
		deadline.val += timeout_us;
		ret = timer_arm(deadline, me);
		ASSERT(ret == EC_SUCCESS);
	}
	while (!(evt = atomic_read_clear(&tsk->events))) {
		/* Remove ourself and get the next task in the scheduler */
		__schedule(1, resched);
		resched = TASK_ID_IDLE;
	}
	if (timeout_us > 0)
		timer_cancel(me);
	return evt;
}

uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait)
{
	task_ *receiver = __task_id_to_ptr(tskid);
	ASSERT(receiver);

	/* Set the event bit in the receiver message bitmap */
	atomic_or(&receiver->events, event);

	/* Re-schedule if priorities have changed */
	if (in_interrupt_context()) {
		/* The receiver might run again */
		atomic_or(&tasks_ready, 1 << tskid);
#ifndef CONFIG_TASK_PROFILING
		if (start_called)
			need_resched_or_profiling = 1;
#endif
	} else {
		if (wait)
			return __wait_evt(-1, tskid);
		else
			__schedule(0, tskid);
	}

	return 0;
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

void task_trigger_irq(int irq)
{
	CPU_NVIC_SWTRIG = irq;
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
		uint8_t irq = __irqprio[i].irq;
		uint8_t prio = __irqprio[i].priority;
		uint32_t prio_shift = irq % 4 * 8 + 5;
		if (prio > 0x7)
			prio = 0x7;
		CPU_NVIC_PRI(irq / 4) =
				(CPU_NVIC_PRI(irq / 4) &
				 ~(0x7 << prio_shift)) |
				(prio << prio_shift);
	}
}

void mutex_lock(struct mutex *mtx)
{
	uint32_t value;
	uint32_t id = 1 << task_get_current();

	ASSERT(id != TASK_ID_INVALID);
	atomic_or(&mtx->waiters, id);

	do {
		/* Try to get the lock (set 1 into the lock field) */
		__asm__ __volatile__("   ldrex   %0, [%1]\n"
				     "   teq     %0, #0\n"
				     "   it eq\n"
				     "   strexeq %0, %2, [%1]\n"
				     : "=&r" (value)
				     : "r" (&mtx->lock), "r" (2) : "cc");
		/*
		 * "value" is equals to 1 if the store conditional failed,
		 * 2 if somebody else owns the mutex, 0 else.
		 */
		if (value == 2)
			/* Contention on the mutex */
			task_wait_event_mask(TASK_EVENT_MUTEX, 0);
	} while (value);

	atomic_clear(&mtx->waiters, id);
}

void mutex_unlock(struct mutex *mtx)
{
	uint32_t waiters;
	task_ *tsk = current_task;

	__asm__ __volatile__("   ldr     %0, [%2]\n"
			     "   str     %3, [%1]\n"
			     : "=&r" (waiters)
			     : "r" (&mtx->lock), "r" (&mtx->waiters), "r" (0)
			     : "cc");
	while (waiters) {
		task_id_t id = 31 - __builtin_clz(waiters);

		/* Somebody is waiting on the mutex */
		task_set_event(id, TASK_EVENT_MUTEX, 0);
		waiters &= ~(1 << id);
	}

	/* Ensure no event is remaining from mutex wake-up */
	atomic_clear(&tsk->events, TASK_EVENT_MUTEX);
}

void task_print_list(void)
{
	int i;

	ccputs("Task Ready Name         Events      Time (s)  StkUsed\n");

	for (i = 0; i < TASK_ID_COUNT; i++) {
		char is_ready = (tasks_ready & (1<<i)) ? 'R' : ' ';
		uint32_t *sp;

		int stackused = tasks_init[i].stack_size;

		for (sp = tasks[i].stack;
		     sp < (uint32_t *)tasks[i].sp && *sp == STACK_UNUSED_VALUE;
		     sp++)
			stackused -= sizeof(uint32_t);

		ccprintf("%4d %c %-16s %08x %11.6ld  %3d/%3d\n", i, is_ready,
			 task_names[i], tasks[i].events, tasks[i].runtime,
			 stackused, tasks_init[i].stack_size);
		cflush();
	}
}

int command_task_info(int argc, char **argv)
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
	ccprintf("Task switching started: %11.6ld s\n", task_start_time);
	ccprintf("Time in tasks:          %11.6ld s\n",
		 get_time().val - task_start_time);
	ccprintf("Time in exceptions:     %11.6ld s\n", exc_total_time);
#endif

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskinfo, command_task_info,
			NULL,
			"Print task info",
			NULL);

#ifdef CONFIG_CMD_TASKREADY
static int command_task_ready(int argc, char **argv)
{
	if (argc < 2) {
		ccprintf("tasks_ready: 0x%08x\n", tasks_ready);
	} else {
		tasks_ready = strtoi(argv[1], NULL, 16);
		ccprintf("Setting tasks_ready to 0x%08x\n", tasks_ready);
		__schedule(0, 0);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskready, command_task_ready,
			"[setmask]",
			"Print/set ready tasks",
			NULL);
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
		 * stack, plus 8 for R4-R11. Even if using FPU, the first frame
		 * does not store FP regs.
		 */
		sp = stack_next + ssize - 16;
		tasks[i].sp = (uint32_t)sp;

		/* Initial context on stack (see __switchto()) */
		sp[8] = tasks_init[i].r0;           /* r0 */
		sp[13] = (uint32_t)task_exit_trap;  /* lr */
		sp[14] = tasks_init[i].pc;          /* pc */
		sp[15] = 0x01000000;                /* psr */

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
	task_start_time = exc_end_time = get_time().val;
#endif
	start_called = 1;

	return __task_start(&need_resched_or_profiling);
}
