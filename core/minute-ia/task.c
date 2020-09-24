/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

/*
 * Needed before headers to support test builds since minute-ia defines 5
 * parameters instead of the normal 4. This specifies a flag value of 0 for all
 * tests tasks.
 */
#define TEST_TASK_EXTRA_ARGS 0

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "link_defs.h"
#include "panic.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "task_defs.h"
#include "interrupts.h"
#include "hpet.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Value to store in unused stack */
#define STACK_UNUSED_VALUE 0xdeadd00d

/* declare task routine prototypes */
#define TASK(n, r, d, s, f) void r(void *);
void __idle(void);
CONFIG_TASK_LIST
CONFIG_TEST_TASK_LIST
#undef TASK

/* This is set by interrupt handlers */
extern volatile uint32_t __in_isr;

/* Task names for easier debugging */
#define TASK(n, r, d, s, f)  #n,
static const char * const task_names[] = {
	"<< idle >>",
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
};
#undef TASK

#ifdef CONFIG_TASK_PROFILING
static uint64_t task_start_time; /* Time task scheduling started */
/*
 * We only keep 32-bit values for exception start/end time, to avoid
 * accounting errors when we service interrupt when the timer wraps around.
 */
static uint32_t exc_start_time;  /* Time of task->exception transition */
static uint32_t exc_end_time;    /* Time of exception->task transition */
static uint64_t exc_total_time;  /* Total time in exceptions */
static uint32_t svc_calls;	 /* Number of service calls */
static uint32_t task_switches;	/* Number of times active task changed */
static uint32_t irq_dist[CONFIG_IRQ_COUNT];	/* Distribution of IRQ calls */
#endif

void __schedule(int desched, int resched);

#ifndef CONFIG_LOW_POWER_IDLE
/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	uint32_t idelay = 1000;

	while (1) {
		/*
		 * Wait for the next irq event.  This stops the CPU clock
		 * (sleep / deep sleep, depending on chip config).
		 *
		 * Todo - implement sleep instead of delay
		 */
		udelay(idelay);
	}
}

#endif /* !CONFIG_LOW_POWER_IDLE */

static void task_exit_trap(void)
{
	int i = task_get_current();

	cprints(CC_TASK, "Task %d (%s) exited!", i, task_get_name(i));
	/* Exited tasks simply sleep forever */
	while (1)
		task_wait_event(-1);
}

/* Startup parameters for all tasks. */
#define TASK(n, r, d, s, f)  {	\
	.r0 = (uint32_t)d,	\
	.pc = (uint32_t)r,	\
	.stack_size = s,	\
	.flags = f,		\
},
static const struct {
	uint32_t r0;
	uint32_t pc;
	uint16_t stack_size;
	uint32_t flags;
} tasks_init[] = {
	TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE, 0)
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
};

#undef TASK

/* Contexts for all tasks */
static task_ tasks[TASK_ID_COUNT];
/* Validity checks about static task invariants */
BUILD_ASSERT(TASK_ID_COUNT <= sizeof(unsigned) * 8);
BUILD_ASSERT(TASK_ID_COUNT < (1 << (sizeof(task_id_t) * 8)));


/* Stacks for all tasks */
#define TASK(n, r, d, s, f)  + s
uint8_t task_stacks[0
		    TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE, 0)
		    CONFIG_TASK_LIST
		    CONFIG_TEST_TASK_LIST
] __aligned(8);

#undef TASK


task_ *current_task, *next_task;

/*
 * Bitmap of all tasks ready to be run.
 *
 * Start off with only the hooks task marked as ready such that all the modules
 * can do their init within a task switching context.  The hooks task will then
 * make a call to enable all tasks.
 */
static uint32_t tasks_ready = BIT(TASK_ID_HOOKS);
/*
 * Initially allow only the HOOKS and IDLE task to run, regardless of ready
 * status, in order for HOOK_INIT to complete before other tasks.
 * task_enable_all_tasks() will open the flood gates.
 */
static uint32_t tasks_enabled = BIT(TASK_ID_HOOKS) | BIT(TASK_ID_IDLE);

static int start_called;  /* Has task swapping started */

static inline task_ *__task_id_to_ptr(task_id_t id)
{
	return tasks + id;
}

void interrupt_disable(void)
{
	__asm__ __volatile__ ("cli");
}

void interrupt_enable(void)
{
	/*
	 * allow enbling interrupt only after task switch is ready
	 */
	ASSERT(task_start_called() != 1);

	__asm__ __volatile__ ("sti");
}

inline int in_interrupt_context(void)
{
	return !!__in_isr;
}

task_id_t task_get_current(void)
{
	/* If we haven't done a context switch then our task ID isn't valid */
	if (IS_ENABLED(CONFIG_DEBUG_BRINGUP))
		ASSERT(task_start_called() != 1);

	return current_task - tasks;
}

const char *task_get_name(task_id_t tskid)
{
	if (tskid < ARRAY_SIZE(task_names))
		return task_names[tskid];

	return "<< unknown >>";
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
uint32_t switch_handler(int desched, task_id_t resched)
{
	task_ *current, *next;

	current = current_task;

	if (IS_ENABLED(CONFIG_DEBUG_STACK_OVERFLOW) &&
	    *current->stack != STACK_UNUSED_VALUE) {
		panic_printf("\n\nStack overflow in %s task!\n",
			     task_get_name(current - tasks));

		if (IS_ENABLED(CONFIG_SOFTWARE_PANIC))
			software_panic(PANIC_SW_STACK_OVERFLOW,
				       current - tasks);
	}

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

	/* Only the first ISR on the (nested IRQ) stack calculates time */
	if (IS_ENABLED(CONFIG_TASK_PROFILING) &&
	    __in_isr == 1) {
		/* Track time in interrupts */
		uint32_t t = get_time().le.lo;

		exc_end_time = t;
		exc_total_time += (t - exc_start_time);
	}

	/* Nothing to do */
	if (next == current)
		return 0;

	/* Switch to new task */
	if (IS_ENABLED(CONFIG_TASK_PROFILING))
		task_switches++;
	next_task = next;

	/* TS required */
	return 1;
}

void __schedule(int desched, int resched)
{
	__asm__ __volatile__("int %0"
			     :
			     : "i"(ISH_TS_VECTOR), "d"(desched), "c"(resched));
}

#ifdef CONFIG_TASK_PROFILING
void __keep task_start_irq_handler(void *data)
{
	/*
	 * Get time before checking depth, in case this handler is
	 * pre-empted.
	 */
	uint32_t t = get_time().le.lo;
	int irq = (uint32_t)data;

	/*
	 * Track IRQ distribution.  No need for atomic add, because an IRQ
	 * can't pre-empt itself. If less than 0, then the vector did not map
	 * to an IRQ but was for a synchronous exception instead (TS_VECTOR)
	 */
	if (irq < CONFIG_IRQ_COUNT)
		irq_dist[irq]++;
	else
		/* Track total number of service calls */
		deprecated_atomic_add(&svc_calls, 1);

	/* Only the outer ISR should keep track of the ISR start time */
	if (__in_isr == 1) {
		exc_start_time = t;

		/*
		 * Bill the current task for time between the end of the last
		 * interrupt and the start of this interrupt (now).
		 */
		current_task->runtime += (t - exc_end_time);
	}
}
#endif

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
	while (!(evt = deprecated_atomic_read_clear(&tsk->events))) {
		/* Remove ourself and get the next task in the scheduler */
		__schedule(1, resched);
		resched = TASK_ID_IDLE;
	}
	if (timeout_us > 0) {
		timer_cancel(me);
		/* Ensure timer event is clear, we no longer care about it */
		deprecated_atomic_clear_bits(&tsk->events, TASK_EVENT_TIMER);
	}
	return evt;
}

uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait)
{
	task_ *receiver = __task_id_to_ptr(tskid);

	if (tskid > TASK_ID_COUNT) {
		receiver = current_task;
		tskid = receiver - tasks;
	} else {
		receiver = __task_id_to_ptr(tskid);
	}

	ASSERT(receiver);

	/* Set the event bit in the receiver message bitmap */
	deprecated_atomic_or(&receiver->events, event);

	/* Re-schedule if priorities have changed */
	if (in_interrupt_context()) {
		/* The receiver might run again */
		deprecated_atomic_or(&tasks_ready, 1 << tskid);
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
		deprecated_atomic_or(&current_task->events,
				     events & ~event_mask);

	return events & event_mask;
}

void task_enable_all_tasks(void)
{
	/* Mark all tasks as ready and table to run. */
	tasks_ready = tasks_enabled = BIT(TASK_ID_COUNT) - 1;

	/* Reschedule the highest priority task. */
	__schedule(0, 0);
}

void task_enable_task(task_id_t tskid)
{
	deprecated_atomic_or(&tasks_enabled, BIT(tskid));
}

void task_disable_task(task_id_t tskid)
{
	deprecated_atomic_clear_bits(&tasks_enabled, BIT(tskid));

	if (!in_interrupt_context() && tskid == task_get_current())
		__schedule(0, 0);
}

void task_enable_irq(int irq)
{
	unmask_interrupt(irq);
}

void __keep task_disable_irq(int irq)
{
	mask_interrupt(irq);
}

void task_clear_pending_irq(int irq)
{
}

void task_trigger_irq(int irq)
{
	/* ISR should not be called before the first task is scheduled */
	if (!task_start_called())
		return;

	/* we don't allow nested interrupt */
	if (in_interrupt_context())
		return;

	/*
	 * "int" instruction accepts vector only as immediate value.
	 * so here, we use one vector(SOFTIRQ_VECTOR) and pass
	 * the address of ISR of irq in ecx register.
	 */
	__asm__ __volatile__("int %0\n" : : "i"(SOFTIRQ_VECTOR), "c"(irq));
}

void mutex_lock(struct mutex *mtx)
{
	uint32_t old_val = 0, value = 1;
	uint32_t id = 1 << task_get_current();

	ASSERT(id != TASK_ID_INVALID);
	deprecated_atomic_or(&mtx->waiters, id);

	do {
		old_val = 0;
		__asm__ __volatile__(
				ASM_LOCK_PREFIX "cmpxchg %1, %2\n"
				: "=a" (old_val)
				: "r" (value), "m" (mtx->lock), "a" (old_val)
				: "memory");

		if (old_val != 0) {
			/* Contention on the mutex */
			task_wait_event_mask(TASK_EVENT_MUTEX, 0);
		}
	} while (old_val);

	deprecated_atomic_clear_bits(&mtx->waiters, id);
}

void mutex_unlock(struct mutex *mtx)
{
	uint32_t waiters = 0;
	uint32_t old_val = 1, val = 0;
	task_ *tsk = current_task;

	__asm__ __volatile__(
			ASM_LOCK_PREFIX "cmpxchg %1, %2\n"
			: "=a" (old_val)
			: "r" (val), "m" (mtx->lock), "a" (old_val)
			: "memory");
	if (old_val == 1)
		waiters = mtx->waiters;
	/* else? Does unlock fail - what to do then ? */
	while (waiters) {
		task_id_t id = __fls(waiters);

		waiters &= ~BIT(id);

		/* Somebody is waiting on the mutex */
		task_set_event(id, TASK_EVENT_MUTEX, 0);
	}

	/* Ensure no event is remaining from mutex wake-up */
	deprecated_atomic_clear_bits(&tsk->events, TASK_EVENT_MUTEX);
}

void task_print_list(void)
{
	int i;

	if (IS_ENABLED(CONFIG_FPU))
		ccputs("Task Ready Name         Events      Time (s)  "
		       "  StkUsed UseFPU\n");
	else
		ccputs("Task Ready Name         Events      Time (s)  "
		       "StkUsed\n");

	for (i = 0; i < TASK_ID_COUNT; i++) {
		char is_ready = (tasks_ready & (1<<i)) ? 'R' : ' ';
		uint32_t *sp;

		int stackused = tasks_init[i].stack_size;

		for (sp = tasks[i].stack;
		     sp < (uint32_t *)tasks[i].sp && *sp == STACK_UNUSED_VALUE;
		     sp++)
			stackused -= sizeof(uint32_t);

		if (IS_ENABLED(CONFIG_FPU)) {
			char use_fpu = tasks[i].use_fpu ? 'Y' : 'N';

			ccprintf("%4d %c %-16s %08x %11.6lld  %3d/%3d %c\n",
				 i, is_ready, task_get_name(i), tasks[i].events,
				 tasks[i].runtime, stackused,
				 tasks_init[i].stack_size, use_fpu);
		} else {
			ccprintf("%4d %c %-16s %08x %11.6lld  %3d/%3d\n",
				 i, is_ready, task_get_name(i), tasks[i].events,
				 tasks[i].runtime, stackused,
				 tasks_init[i].stack_size);
		}

		cflush();
	}
}

int command_task_info(int argc, char **argv)
{
	task_print_list();

	if (IS_ENABLED(CONFIG_TASK_PROFILING)) {
		int total = 0;
		int i;

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
		ccprintf("Task switching started: %11.6lld s\n",
			 task_start_time);
		ccprintf("Time in tasks:          %11.6lld s\n",
			 get_time().val - task_start_time);
		ccprintf("Time in exceptions:     %11.6lld s\n",
			 exc_total_time);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskinfo, command_task_info,
			NULL,
			"Print task info");

__maybe_unused
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

#ifdef CONFIG_CMD_TASKREADY
DECLARE_CONSOLE_COMMAND(taskready, command_task_ready,
			"[setmask]",
			"Print/set ready tasks");
#endif

void task_pre_init(void)
{
	int i, cs;
	uint32_t *stack_next = (uint32_t *)task_stacks;

	__asm__ __volatile__ ("movl %%cs, %0":"=r" (cs));

	/* Fill the task memory with initial values */
	for (i = 0; i < TASK_ID_COUNT; i++) {
		uint32_t *sp;
		/* Stack size in words */
		uint32_t ssize = tasks_init[i].stack_size / 4;

		tasks[i].stack = stack_next;

		/*
		 * Update stack used by first frame: 8 words for the register
		 * stack, plus 8 for task context.
		 */
		sp = stack_next + ssize - 16;
		tasks[i].sp = (uint32_t)sp;

		/* Initial context on stack (see __switchto()) */

		/* For POPA */
#if 0
		/* For debug */
		sp[0] = 0xee;	/* EDI */
		sp[1] = 0xe5;	/* ESI */
		sp[2] = 0x00;	/* EBP */
		sp[3] = 0x00;	/* ESP - ignored anyway */
		sp[4] = 0xeb1;	/* EBX */
		sp[5] = 0xed1;	/* EDX */
		sp[6] = 0xec;	/* ECX */
		sp[7] = 0xea;	/* EAX */
#endif
		/* For IRET */
		sp[8] = tasks_init[i].pc;	/* pc */
		sp[9] = cs;
		sp[10] = INITIAL_EFLAGS;

		sp[11] = (uint32_t) task_exit_trap;
		sp[12] = tasks_init[i].r0;	/* task argument */
		sp[13] = 0x00;
		sp[14] = 0x00;
		sp[15] = 0x00;

		if (IS_ENABLED(CONFIG_FPU)) {
			static uint8_t default_fp_ctx[] = {
				/* Initial FP state */
				0x7f, 0x00, /* Control[0-15] */
				0xff, 0xff, /* unused */
				0x00, 0x00, /* Status[0-15] */
				0xff, 0xff, /* unused */
				0xff, 0xff, /* Tag[0-15] */
				0xff, 0xff};/* unused */

			/* Copy default x86 FPU state for each task */
			memcpy(tasks[i].fp_ctx, default_fp_ctx,
			       sizeof(default_fp_ctx));

			if (tasks_init[i].flags & MIA_TASK_FLAG_USE_FPU)
				tasks[i].use_fpu = 1;
		}

		/* Fill unused stack; also used to detect stack overflow. */
		for (sp = stack_next; sp < (uint32_t *)tasks[i].sp; sp++)
			*sp = STACK_UNUSED_VALUE;

		stack_next += ssize;
	}

	current_task = __task_id_to_ptr(1);

	/* Initialize IRQs */
	init_interrupts();
}

void task_clear_fp_used(void)
{
}

int task_start(void)
{
	if (IS_ENABLED(CONFIG_TASK_PROFILING))
		task_start_time = exc_end_time = get_time().val;

	return __task_start(&start_called);
}
