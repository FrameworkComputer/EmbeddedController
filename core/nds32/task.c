/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hwtimer_chip.h"
#include "intc.h"
#include "irq_chip.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
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
static int task_will_switch;
static uint64_t exc_sub_time;
static uint64_t task_start_time; /* Time task scheduling started */
static uint64_t exc_start_time;  /* Time of task->exception transition */
static uint64_t exc_end_time;    /* Time of exception->task transition */
static uint64_t exc_total_time;  /* Total time in exceptions */
static uint32_t svc_calls;       /* Number of service calls */
static uint32_t task_switches;   /* Number of times active task changed */
static uint32_t irq_dist[CONFIG_IRQ_COUNT];  /* Distribution of IRQ calls */
#endif

extern int __task_start(void);

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
#ifdef CHIP_FAMILY_IT83XX
		/* doze mode */
		IT83XX_ECPM_PLLCTRL = EC_PLL_DOZE;
#endif
		asm volatile ("dsb");
		/*
		 * Wait for the next irq event.  This stops the CPU clock
		 * (sleep / deep sleep, depending on chip config).
		 */
		asm("standby wake_grant");
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
#ifdef CONFIG_FPU
uint32_t scratchpad[19+18];
#else
uint32_t scratchpad[19];
#endif

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
static uint32_t tasks_ready = (1 << TASK_ID_HOOKS);

static int start_called;  /* Has task swapping started */

/* interrupt number of sw interrupt */
static int sw_int_num;

/* Number of CPU hardware interrupts (HW0 ~ HW15) */
int cpu_int_entry_number;

static inline task_ *__task_id_to_ptr(task_id_t id)
{
	return tasks + id;
}

/*
 * We use INT_MASK to enable (interrupt_enable)/
 * disable (interrupt_disable) all maskable interrupts.
 * And, EC modules share HW2 ~ HW15 interrupts. If corresponding
 * bit of INT_MASK is set, it will never be cleared
 * (see chip_disable_irq()). To enable/disable individual
 * interrupt of EC module, we can use corresponding EXT_IERx registers.
 *
 * ------------     -----------
 * |          |     | ------- |
 * |EC modules|     | | HW2 | |
 * |          |     | ------- |
 * | INT 0    |     | ------- |    -------    -------
 * | ~        | --> | | HW3 | | -> | GIE | -> | CPU |
 * | INT 167  |     | ------- |    -------    -------
 * |          |     |   ...   |       |
 * |          |     |   ...   |        - clear by HW while
 * |          |     | ------- |          interrupt occur and
 * |          |     | | HW15| |          restore from IPSW after
 * |          |     | ------- |          instruction "iret".
 * | EXT_IERx |     | INT_MASK|
 * ------------     -----------
 */
void interrupt_disable(void)
{
	/* Mask all interrupts, only keep division by zero exception */
	uint32_t val = (1 << 30);
	asm volatile ("mtsr %0, $INT_MASK" : : "r"(val));
	asm volatile ("dsb");
}

void interrupt_enable(void)
{
	/* Enable HW2 ~ HW15 and division by zero exception interrupts */
	uint32_t val = ((1 << 30) | 0xFFFC);
	asm volatile ("mtsr %0, $INT_MASK" : : "r"(val));
}

inline int in_interrupt_context(void)
{
	/* check INTL (Interrupt Stack Level) bits */
	return get_psw() & PSW_INTL_MASK;
}

task_id_t task_get_current(void)
{
#ifdef CONFIG_DEBUG_BRINGUP
	/* If we haven't done a context switch then our task ID isn't valid */
	ASSERT(current_task != (task_ *)scratchpad);
#endif
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

int get_sw_int(void)
{
	/* If this is a SW interrupt */
	if (get_itype() & 8)
		return sw_int_num;
	return 0;
}

/**
 * Scheduling system call
 *
 * Also includes emulation of software triggering interrupt vector
 */
void syscall_handler(int desched, task_id_t resched, int swirq)
{
	/* are we emulating an interrupt ? */
	if (swirq) {
		void (*handler)(void) = __irqhandler[swirq + 1];
		/* adjust IPC to return *after* the syscall instruction */
		set_ipc(get_ipc() + 4);
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
	set_ipc(get_ipc() + 4);
}

task_ *next_sched_task(void)
{
	task_ *new_task = __task_id_to_ptr(__fls(tasks_ready));

#ifdef CONFIG_TASK_PROFILING
	if (current_task != new_task) {
		if ((current_task - tasks) < TASK_ID_COUNT) {
			current_task->runtime +=
				(exc_start_time - exc_end_time - exc_sub_time);
		}
		task_will_switch = 1;
	}
#endif

#ifdef CONFIG_DEBUG_STACK_OVERFLOW
	if (*current_task->stack != STACK_UNUSED_VALUE) {
		int i = task_get_current();
		if (i < TASK_ID_COUNT) {
			panic_printf("\n\nStack overflow in %s task!\n",
				task_names[i]);
#ifdef CONFIG_SOFTWARE_PANIC
		software_panic(PANIC_SW_STACK_OVERFLOW, i);
#endif
		}
	}
#endif

	return new_task;
}

static inline void __schedule(int desched, int resched, int swirq)
{
	register int p0 asm("$r0") = desched;
	register int p1 asm("$r1") = resched;
	register int p2 asm("$r2") = swirq;

	asm("syscall 0" : : "r"(p0), "r"(p1), "r"(p2));
}

void update_exc_start_time(void)
{
#ifdef CONFIG_TASK_PROFILING
	exc_start_time = get_time().val;
#endif
}

/* Interrupt number of EC modules */
static volatile int ec_int;

#ifdef CHIP_FAMILY_IT83XX
int intc_get_ec_int(void)
{
	return ec_int;
}
#endif

void start_irq_handler(void)
{
	/* save r0, r1, and r2 for syscall */
	asm volatile ("smw.adm $r0, [$sp], $r2, 0");
	/* If this is a SW interrupt */
	if (get_itype() & 8) {
		ec_int = get_sw_int();
	} else {
#ifdef CHIP_FAMILY_IT83XX
		int i;

		for (i = 0; i < IT83XX_IRQ_COUNT; i++) {
			ec_int = IT83XX_INTC_IVCT(cpu_int_entry_number);
			/*
			 * WORKAROUND: when the interrupt vector register isn't
			 * latched in a load operation,
			 * we read it again to make sure the value we got
			 * is the correct value.
			 */
			if (ec_int == IT83XX_INTC_IVCT(cpu_int_entry_number))
				break;
		}
		/* Determine interrupt number */
		ec_int -= 16;
#endif
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
	/* restore r0, r1, and r2 */
	asm volatile ("lmw.bim $r0, [$sp], $r2, 0");
}

void end_irq_handler(void)
{
#ifdef CONFIG_TASK_PROFILING
	uint64_t t, p;
	/*
	 * save r0 and fp (fp for restore r0-r5, r15, fp, lp and sp
	 * while interrupt exit.
	 */
	asm volatile ("smw.adm $r0, [$sp], $r0, 8");

	t = get_time().val;
	p = t - exc_start_time;

	exc_total_time += p;
	exc_sub_time += p;
	if (task_will_switch) {
		task_will_switch = 0;
		exc_sub_time = 0;
		exc_end_time = t;
		task_switches++;
	}

	/* restore r0 and fp */
	asm volatile ("lmw.bim $r0, [$sp], $r0, 8");
#endif
}

static uint32_t __wait_evt(int timeout_us, task_id_t resched)
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
	while (!(evt = atomic_read_clear(&tsk->events))) {
		/* Remove ourself and get the next task in the scheduler */
		__schedule(1, resched, 0);
		resched = TASK_ID_IDLE;
	}
	if (timeout_us > 0) {
		timer_cancel(me);
		/* Ensure timer event is clear, we no longer care about it */
		atomic_clear(&tsk->events, TASK_EVENT_TIMER);
	}
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
		need_resched = 1;
	} else {
		if (wait)
			return __wait_evt(-1, tskid);
		else
			__schedule(0, tskid, 0);
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

uint32_t get_int_mask(void)
{
	uint32_t ret;
	asm volatile ("mfsr %0, $INT_MASK" : "=r"(ret));
	return ret;
}

void set_int_mask(uint32_t val)
{
	asm volatile ("mtsr %0, $INT_MASK" : : "r"(val));
}

static void set_int_priority(uint32_t val)
{
	asm volatile ("mtsr %0, $INT_PRI" : : "r"(val));
}

uint32_t get_int_ctrl(void)
{
	uint32_t ret;

	asm volatile ("mfsr %0, $INT_CTRL" : "=r"(ret));
	return ret;
}

void set_int_ctrl(uint32_t val)
{
	asm volatile ("mtsr %0, $INT_CTRL" : : "r"(val));
}

void task_enable_all_tasks(void)
{
	/* Mark all tasks are ready to run. */
	tasks_ready = (1 << TASK_ID_COUNT) - 1;
	/* Reschedule the highest priority task. */
	__schedule(0, 0, 0);
}

void task_enable_irq(int irq)
{
	uint32_t int_mask = get_int_mask();

	interrupt_disable();
	chip_enable_irq(irq);
	set_int_mask(int_mask);
}

void task_disable_irq(int irq)
{
	uint32_t int_mask = get_int_mask();

	interrupt_disable();
	chip_disable_irq(irq);
	set_int_mask(int_mask);
}

void task_clear_pending_irq(int irq)
{
	chip_clear_pending_irq(irq);
}

void task_trigger_irq(int irq)
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
	/* Get the IRQ priorities section from the linker */
	int exc_calls = __irqprio_end - __irqprio;
	int i;
	uint32_t all_priorities = 0;

	/* chip-specific interrupt controller initialization */
	chip_init_irqs();

	/*
	 * bit0 @ INT_CTRL = 0,
	 * Interrupts still keep programmable priority level.
	 */
	set_int_ctrl((get_int_ctrl() & ~(1 << 0)));

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
		all_priorities |= (prio & 0x3)  << (irq * 2);
	}
	set_int_priority(all_priorities);
}

void mutex_lock(struct mutex *mtx)
{
	uint32_t id = 1 << task_get_current();

	ASSERT(id != TASK_ID_INVALID);

	/* critical section with interrupts off */
	interrupt_disable();
	mtx->waiters |= id;
	while (1) {
		if (!mtx->lock) { /* we got it ! */
			mtx->lock = 2;
			mtx->waiters &= ~id;
			/* end of critical section : re-enable interrupts */
			interrupt_enable();
			return;
		} else { /* Contention on the mutex */
			/* end of critical section : re-enable interrupts */
			interrupt_enable();
			/* Sleep waiting for our turn */
			task_wait_event_mask(TASK_EVENT_MUTEX, 0);
			/* re-enter critical section */
			interrupt_disable();
		}
	}
}

void mutex_unlock(struct mutex *mtx)
{
	uint32_t waiters;
	task_ *tsk = current_task;

	waiters = mtx->waiters;
	/* give back the lock */
	mtx->lock = 0;

	while (waiters) {
		task_id_t id = __fls(waiters);
		waiters &= ~(1 << id);

		/* Somebody is waiting on the mutex */
		task_set_event(id, TASK_EVENT_MUTEX, 0);
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
		 * Update stack used by first frame: 15 regs + PC + PSW
		 */
		sp = stack_next + ssize - 17;
		tasks[i].sp = (uint32_t)sp;

		/* Initial context on stack (see __switchto()) */
		sp[7] = tasks_init[i].r0;           /* r0 */
		sp[15] = (uint32_t)task_exit_trap;  /* lr */
		sp[1] = tasks_init[i].pc;           /* pc */
		sp[0] = 0x70009;                    /* psw */
		sp[16] = (uint32_t)(sp + 17);       /* sp */

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
	task_start_time = exc_end_time = get_time().val;
#endif
	start_called = 1;

	return __task_start();
}
