/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include "atomic.h"
#include "console.h"
#include "cpu.h"
#include "irq_chip.h"
#include "link_defs.h"
#include "task.h"
#include "timer.h"
#include "util.h"

typedef struct {
	/*
	 * Note that sp must be the first element in the task struct
	 * for __switchto() to work.
	 */
	uint32_t sp;       /* Saved stack pointer for context switch */
	uint32_t events;   /* Bitmaps of received events */
	uint64_t runtime;  /* Time spent in task */
	uint32_t *stack;   /* Start of stack */
} task_;

/* Value to store in unused stack */
#define STACK_UNUSED_VALUE 0xdeadd00d

/* declare task routine prototypes */
#define TASK(n, r, d, s) void r(void *);
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
static int task_will_switch;
static uint32_t exc_sub_time;
static uint64_t task_start_time; /* Time task scheduling started */
static uint32_t exc_start_time;  /* Time of task->exception transition */
static uint32_t exc_end_time;    /* Time of exception->task transition */
static uint64_t exc_total_time;  /* Total time in exceptions */
static uint32_t svc_calls;       /* Number of service calls */
static uint32_t task_switches;   /* Number of times active task changed */
static uint32_t irq_dist[CONFIG_IRQ_COUNT];  /* Distribution of IRQ calls */
#endif

extern int __task_start(void);

#if defined(CHIP_FAMILY_IT83XX)
extern void clock_sleep_mode_wakeup_isr(void);
#endif

#ifndef CONFIG_LOW_POWER_IDLE
/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	/*
	 * Print when the idle task starts.  This is the lowest priority task,
	 * so this only starts once all other tasks have gotten a chance to do
	 * their task inits and have gone to sleep.
	 */
	cprints(CC_TASK, "idle task started");

	while (1) {
#if defined(CHIP_FAMILY_IT83XX)
		/* doze mode */
		IT83XX_ECPM_PLLCTRL = EC_PLL_DOZE;
		clock_cpu_standby();
#else
		asm("wfi");
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
#define TASK(n, r, d, s)  {	\
	.a0 = (uint32_t)d,	\
	.pc = (uint32_t)r,	\
	.stack_size = s,	\
},
static const struct {
	uint32_t a0;
	uint32_t pc;
	uint16_t stack_size;
} tasks_init[] = {
	TASK(IDLE, __idle, 0, IDLE_TASK_STACK_SIZE)
	CONFIG_TASK_LIST
	CONFIG_TEST_TASK_LIST
};
#undef TASK

/* Contexts for all tasks */
static task_ tasks[TASK_ID_COUNT] __attribute__ ((section(".bss.tasks")));
/* Validity checks about static task invariants */
BUILD_ASSERT(TASK_ID_COUNT <= (sizeof(unsigned) * 8));
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
uint32_t scratchpad[TASK_SCRATCHPAD_SIZE] __attribute__
					((section(".bss.task_scratchpad")));

task_ *current_task = (task_ *)scratchpad;

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
int need_resched;

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

int start_called;  /* Has task swapping started */

/* in interrupt context */
static volatile int in_interrupt;
/* Interrupt number of EC modules */
volatile int ec_int;
/* Interrupt group of EC INTC modules */
volatile int ec_int_group;
/* interrupt number of sw interrupt */
static int sw_int_num;
/* This variable is used to save return address register at EC reset. */
uint32_t ec_reset_lp;
/*
 * This variable is used to save return address register,
 * and it is updated at the beginning of each ISR.
 */
uint32_t ira;

static inline task_ *__task_id_to_ptr(task_id_t id)
{
	return tasks + id;
}

void __ram_code interrupt_disable(void)
{
	/* bit11: disable MEIE */
	asm volatile ("li t0, 0x800");
	asm volatile ("csrc mie, t0");
}

void __ram_code interrupt_enable(void)
{
	/* bit11: enable MEIE */
	asm volatile ("li t0, 0x800");
	asm volatile ("csrs  mie, t0");
}

inline int in_interrupt_context(void)
{
	return in_interrupt;
}

int in_soft_interrupt_context(void)
{
	/* group 16 is reserved for soft-irq */
	return in_interrupt_context() && ec_int_group == 16;
}

task_id_t __ram_code task_get_current(void)
{
#ifdef CONFIG_DEBUG_BRINGUP
	/* If we haven't done a context switch then our task ID isn't valid */
	ASSERT(current_task != (task_ *)scratchpad);
#endif
	return current_task - tasks;
}

uint32_t * __ram_code task_get_event_bitmap(task_id_t tskid)
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
 *
 * Also includes emulation of software triggering interrupt vector
 */
void __ram_code __keep syscall_handler(int desched, task_id_t resched,
								int swirq)
{
	/* are we emulating an interrupt ? */
	if (swirq) {
		void (*handler)(void) = __irqhandler[swirq];
		/* adjust IPC to return *after* the syscall instruction */
		set_mepc(get_mepc() + 4);
		/* call the regular IRQ handler */
		handler();
		sw_int_num = 0;
		return;
	}

	if (desched && !current_task->events) {
		/*
		 * Remove our own ready bit (current - tasks is same as
		 * task_get_current())
		 */
		tasks_ready &= ~(1 << (current_task - tasks));
	}
	tasks_ready |= 1 << resched;

	/* trigger a re-scheduling on exit */
	need_resched = 1;

#ifdef CONFIG_TASK_PROFILING
	svc_calls++;
#endif
	/* adjust IPC to return *after* the syscall instruction */
	set_mepc(get_mepc() + 4);
}

task_ * __ram_code next_sched_task(void)
{
	task_ *new_task = __task_id_to_ptr(__fls(tasks_ready & tasks_enabled));

#ifdef CONFIG_TASK_PROFILING
	if (current_task != new_task) {
		current_task->runtime +=
				(exc_start_time - exc_end_time - exc_sub_time);
		task_will_switch = 1;
	}
#endif

#ifdef CONFIG_DEBUG_STACK_OVERFLOW
	if (*current_task->stack != STACK_UNUSED_VALUE) {
		int i = task_get_current();

		panic_printf("\n\nStack overflow in %s task!\n", task_names[i]);
#ifdef CONFIG_SOFTWARE_PANIC
		software_panic(PANIC_SW_STACK_OVERFLOW, i);
#endif
	}
#endif

	return new_task;
}

static inline void __schedule(int desched, int resched, int swirq)
{
	register int p0 asm("a0") = desched;
	register int p1 asm("a1") = resched;
	register int p2 asm("a2") = swirq;

	asm("ecall" : : "r"(p0), "r"(p1), "r"(p2));
}

void __ram_code update_exc_start_time(void)
{
#ifdef CONFIG_TASK_PROFILING
	exc_start_time = get_time().le.lo;
#endif
}

void __ram_code start_irq_handler(void)
{
	/* save a0, a1, and a2 for syscall */
	asm volatile ("addi sp, sp, -4*3");
	asm volatile ("sw a0, 0(sp)");
	asm volatile ("sw a1, 1*4(sp)");
	asm volatile ("sw a2, 2*4(sp)");

	in_interrupt = 1;

	/* If this is a SW interrupt */
	if (get_mcause() == 11) {
		ec_int = sw_int_num;
		ec_int_group = 16;
	} else {
		/*
		 * Determine interrupt number.
		 * -1 if it cannot find the corresponding interrupt source.
		 */
		ec_int = chip_get_ec_int();
		if (ec_int == -1)
			goto error;
		ec_int_group = chip_get_intc_group(ec_int);
	}

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CHIP_FAMILY_IT83XX)
	clock_sleep_mode_wakeup_isr();
#endif
#ifdef CONFIG_TASK_PROFILING
	update_exc_start_time();

	/*
	 * Track IRQ distribution.  No need for atomic add, because an IRQ
	 * can't pre-empt itself.
	 */
	if ((ec_int > 0) && (ec_int < ARRAY_SIZE(irq_dist)))
		irq_dist[ec_int]++;
#endif

error:
	/* cannot use return statement because a0 has been used */
	asm volatile ("add t0, zero, %0" :: "r"(ec_int));

	/* restore a0, a1, and a2 */
	asm volatile ("lw a0, 0(sp)");
	asm volatile ("lw a1, 1*4(sp)");
	asm volatile ("lw a2, 2*4(sp)");
	asm volatile ("addi sp, sp, 4*3");
}

void __ram_code end_irq_handler(void)
{
#ifdef CONFIG_TASK_PROFILING
	uint32_t t, p;

	t = get_time().le.lo;
	p = t - exc_start_time;

	exc_total_time += p;
	exc_sub_time += p;
	if (task_will_switch) {
		task_will_switch = 0;
		exc_sub_time = 0;
		exc_end_time = t;
		task_switches++;
	}
#endif
	in_interrupt = 0;
}

static uint32_t __ram_code __wait_evt(int timeout_us, task_id_t resched)
{
	task_ *tsk = current_task;
	task_id_t me = tsk - tasks;
	uint32_t evt;
	int ret;

	ASSERT(!in_interrupt_context());

	if (timeout_us > 0) {
		timestamp_t deadline = get_time();

		deadline.val += timeout_us;
		ret = timer_arm(deadline, me);
		ASSERT(ret == EC_SUCCESS);
	}
	while (!(evt = deprecated_atomic_read_clear(&tsk->events))) {
		/* Remove ourself and get the next task in the scheduler */
		__schedule(1, resched, 0);
		resched = TASK_ID_IDLE;
	}
	if (timeout_us > 0) {
		timer_cancel(me);
		/* Ensure timer event is clear, we no longer care about it */
		deprecated_atomic_clear_bits(&tsk->events, TASK_EVENT_TIMER);
	}
	return evt;
}

uint32_t __ram_code task_set_event(task_id_t tskid, uint32_t event, int wait)
{
	task_ *receiver = __task_id_to_ptr(tskid);

	ASSERT(receiver);

	/* Set the event bit in the receiver message bitmap */
	deprecated_atomic_or(&receiver->events, event);

	/* Re-schedule if priorities have changed */
	if (in_interrupt_context()) {
		/* The receiver might run again */
		deprecated_atomic_or(&tasks_ready, 1 << tskid);
		if (start_called)
			need_resched = 1;
	} else {
		if (wait)
			return __wait_evt(-1, tskid);
		else
			__schedule(0, tskid, 0);
	}

	return 0;
}

uint32_t __ram_code task_wait_event(int timeout_us)
{
	return __wait_evt(timeout_us, TASK_ID_IDLE);
}

uint32_t __ram_code task_wait_event_mask(uint32_t event_mask, int timeout_us)
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

uint32_t __ram_code read_clear_int_mask(void)
{
	uint32_t mie, meie = BIT(11);

	/* Read and clear MEIE bit of MIE register. */
	asm volatile ("csrrc %0, mie, %1" : "=r"(mie) : "r"(meie));

	return mie;
}

void __ram_code set_int_mask(uint32_t val)
{
	asm volatile ("csrw mie, %0" : : "r"(val));
}

void task_enable_all_tasks(void)
{
	/* Mark all tasks as ready and able to run. */
	tasks_ready = tasks_enabled = BIT(TASK_ID_COUNT) - 1;
	/* Reschedule the highest priority task. */
	__schedule(0, 0, 0);
}

void task_enable_task(task_id_t tskid)
{
	deprecated_atomic_or(&tasks_enabled, BIT(tskid));
}

void task_disable_task(task_id_t tskid)
{
	deprecated_atomic_clear_bits(&tasks_enabled, BIT(tskid));

	if (!in_interrupt_context() && tskid == task_get_current())
		__schedule(0, 0, 0);
}

void __ram_code task_enable_irq(int irq)
{
	uint32_t int_mask = read_clear_int_mask();

	chip_enable_irq(irq);
	set_int_mask(int_mask);
}

void __ram_code task_disable_irq(int irq)
{
	uint32_t int_mask = read_clear_int_mask();

	chip_disable_irq(irq);
	set_int_mask(int_mask);
}

void __ram_code task_clear_pending_irq(int irq)
{
	chip_clear_pending_irq(irq);
}

void __ram_code task_trigger_irq(int irq)
{
	int cpu_int = chip_trigger_irq(irq);

	if (cpu_int > 0) {
		sw_int_num = irq;
		__schedule(0, 0, cpu_int);
	}
}

/*
 * Initialize IRQs in the IVIC and set their priorities as defined by the
 * DECLARE_IRQ statements.
 */
static void ivic_init_irqs(void)
{
	/* chip-specific interrupt controller initialization */
	chip_init_irqs();
	/*
	 * Re-enable global interrupts in case they're disabled.  On a reboot,
	 * they're already enabled; if we've jumped here from another image,
	 * they're not.
	 */
	interrupt_enable();
}

void __ram_code mutex_lock(struct mutex *mtx)
{
	uint32_t locked;
	uint32_t id = 1 << task_get_current();

	ASSERT(id != TASK_ID_INVALID);
	deprecated_atomic_or(&mtx->waiters, id);

	while (1) {
		asm volatile (
			/* set lock value */
			"li %0, 2\n\t"
			/* attempt to acquire lock */
			"amoswap.w.aq %0, %0, %1\n\t"
			: "=r" (locked), "+A" (mtx->lock));
		/* we got it ! */
		if (!locked)
			break;
		/* Contention on the mutex */
		/* Sleep waiting for our turn */
		task_wait_event_mask(TASK_EVENT_MUTEX, 0);
	}

	deprecated_atomic_clear_bits(&mtx->waiters, id);
}

void __ram_code mutex_unlock(struct mutex *mtx)
{
	uint32_t waiters;
	task_ *tsk = current_task;

	/* give back the lock */
	asm volatile (
		"amoswap.w.aqrl zero, zero, %0\n\t"
		: "+A" (mtx->lock));
	waiters = mtx->waiters;

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

	ccputs("Task Ready Name         Events      Time (s)  StkUsed\n");

	for (i = 0; i < TASK_ID_COUNT; i++) {
		char is_ready = (tasks_ready & (1<<i)) ? 'R' : ' ';
		uint32_t *sp;

		int stackused = tasks_init[i].stack_size;

		for (sp = tasks[i].stack;
		     sp < (uint32_t *)tasks[i].sp && *sp == STACK_UNUSED_VALUE;
		     sp++)
			stackused -= sizeof(uint32_t);

		ccprintf("%4d %c %-16s %08x %11.6lld  %3d/%3d\n", i, is_ready,
			 task_names[i], tasks[i].events, tasks[i].runtime,
			 stackused, tasks_init[i].stack_size);
		cflush();
	}
}

int command_task_info(int argc, char **argv)
{
#ifdef CONFIG_TASK_PROFILING
	unsigned int total = 0;
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

	ccprintf("Service calls:          %11u\n", svc_calls);
	ccprintf("Total exceptions:       %11u\n", total + svc_calls);
	ccprintf("Task switches:          %11u\n", task_switches);
	ccprintf("Task switching started: %11.6llu s\n", task_start_time);
	ccprintf("Time in tasks:          %11.6llu s\n",
		 get_time().val - task_start_time);
	ccprintf("Time in exceptions:     %11.6llu s\n", exc_total_time);
#endif

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskinfo, command_task_info,
			NULL,
			"Print task info");

static int command_task_ready(int argc, char **argv)
{
	if (argc < 2) {
		ccprintf("tasks_ready: 0x%08x\n", tasks_ready);
	} else {
		tasks_ready = strtoi(argv[1], NULL, 16);
		ccprintf("Setting tasks_ready to 0x%08x\n", tasks_ready);
		__schedule(0, 0, 0);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskready, command_task_ready,
			"[setmask]",
			"Print/set ready tasks");

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
		 * Update stack used by first frame: 28 regs + MEPC + (FP regs)
		 */
		sp = stack_next + ssize - TASK_SCRATCHPAD_SIZE;
		tasks[i].sp = (uint32_t)sp;

		/* Initial context on stack (see __switchto()) */
		sp[TASK_SCRATCHPAD_SIZE-2] = tasks_init[i].a0;         /* a0 */
		sp[TASK_SCRATCHPAD_SIZE-1] = (uint32_t)task_exit_trap; /* ra */
		sp[0] = tasks_init[i].pc;  /* pc/mepc */

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
	ivic_init_irqs();
}

int task_start(void)
{
#ifdef CONFIG_TASK_PROFILING
	task_start_time = get_time().val;
	exc_end_time = get_time().le.lo;
#endif

	return __task_start();
}
