/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include <stdint.h>

#include "config.h"
#include "atomic.h"
#include "console.h"
#include "cpu.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/**
 * Global memory size for a task : 512 bytes
 * including its contexts and its stack
 */
#define TASK_SIZE_LOG2 9
#define TASK_SIZE      (1<<TASK_SIZE_LOG2)

typedef union {
	struct {
		uint32_t sp;       /* saved stack pointer for context switch */
		uint32_t events;   /* bitmaps of received events */
		uint32_t guard;    /* Guard value to detect stack overflow */
		uint8_t stack[0];  /* task stack */
	};
	uint32_t context[TASK_SIZE/4];
} task_;

/* declare task routine prototypes */
#define TASK(n, r, d) int r(void *);
#include TASK_LIST
static void __idle(void);
CONFIG_TASK_LIST
#undef TASK

/* store the task names for easier debugging */
#define TASK(n, r, d)  #n,
#include TASK_LIST
static const char * const task_names[] = {
	"<< idle >>",
	CONFIG_TASK_LIST
};
#undef TASK

extern void __switchto(task_ *from, task_ *to);


/* Idle task.  Executed when no tasks are ready to be scheduled. */
void __idle(void)
{
	while (1) {
		/* Wait for the irq event */
		asm("wfi");
		/* TODO: more power management here */
	}
}


static void task_exit_trap(void)
{
	int i = task_get_current();
	uart_printf("[Task %d (%s) exited!]\n", i, task_names[i]);
	/* Exited tasks simply sleep forever */
	while (1)
		task_wait_event(-1);
}


#define GUARD_VALUE 0x12345678

/* Declare and fill the contexts for all tasks. Note that while it would be
 * more readable to use the struct fields (.sp, .guard) where applicable, gcc
 * can't mix initializing some fields on one side of the union and some fields
 * on the other, so we have to use .context for all initialization.
 */
#define TASK(n, r, d)  {						\
	.context[0] = (uint32_t)(tasks + TASK_ID_##n + 1) - 64,	        \
	.context[2] = GUARD_VALUE,					\
	.context[TASK_SIZE/4 - 8/*r0*/] = (uint32_t)d,                  \
	.context[TASK_SIZE/4 - 3/*lr*/] = (uint32_t)task_exit_trap,     \
	.context[TASK_SIZE/4 - 2/*pc*/] = (uint32_t)r,                  \
	.context[TASK_SIZE/4 - 1/*psr*/] = 0x01000000 },
#include TASK_LIST
static task_ tasks[] __attribute__((section(".data.tasks")))
		__attribute__((aligned(TASK_SIZE))) = {
	TASK(IDLE, __idle, 0)
	CONFIG_TASK_LIST
};
#undef TASK
/* Reserve space to discard context on first context switch. */
uint32_t scratchpad[17] __attribute__((section(".data.tasks")));

/* context switch at the next exception exit if needed */
/* TODO: who sets this back to 0 after it's set to 1? */
static int need_resched = 0;

/**
 * bitmap of all tasks ready to be run
 *
 * Currently all tasks are enabled at startup.
 */
static uint32_t tasks_ready = (1<<TASK_ID_COUNT) - 1;


static task_ *__get_current(void)
{
	unsigned sp;

	asm("mov %0, sp":"=r"(sp));
	return (task_ *)((sp - 4) & ~(TASK_SIZE-1));
}


/**
 * Return a pointer to the task preempted by the current exception
 *
 * designed to be called from interrupt context.
 */
static task_ *__get_task_scheduled(void)
{
	unsigned sp;

	asm("mrs %0, psp":"=r"(sp));
	return (task_ *)((sp - 16) & ~(TASK_SIZE-1));
}


static inline task_ *__task_id_to_ptr(task_id_t id)
{
	return tasks + id;
}

/* Disables CPU interrupt */
void interrupt_disable(void)
{
	asm("cpsid i");
}

/* Enables CPU interrupt bit. */
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


task_id_t task_get_current(void)
{
	task_id_t id = __get_current() - tasks;
	if (id >= TASK_ID_COUNT)
		id = TASK_ID_INVALID;

	return id;
}


uint32_t *task_get_event_bitmap(task_id_t tskid)
{
	task_ *tsk = __task_id_to_ptr(tskid);
	return &tsk->events;
}


/**
 * scheduling system call
 */
void svc_handler(int desched, task_id_t resched)
{
	task_ *current, *next;

	/* push the priority to -1 until the return, to avoid being
	 * interrupted */
	asm volatile("cpsid f\n"
		     "isb\n");
	current = __get_task_scheduled();
#ifdef CONFIG_OVERFLOW_DETECT
	ASSERT(current->guard == GUARD_VALUE);
#endif

	if (desched && !current->events) {
		/* Remove our own ready bit */
		tasks_ready &= ~(1 << (current-tasks));
	}
	tasks_ready |= 1 << resched;

	ASSERT(tasks_ready);
	next = __task_id_to_ptr(31 - __builtin_clz(tasks_ready));

	/* Nothing to do */
	if (next == current)
		return;

	__switchto(current, next);
}


void __schedule(int desched, int resched)
{
	register int p0 asm("r0") = desched;
	register int p1 asm("r1") = resched;
	/* TODO remove hardcoded opcode
	 * SWI not compiled properly for ARMv7-M on our current chroot toolchain
	 */
	asm(".hword 0xdf00 @swi 0"::"r"(p0),"r"(p1));
}


void task_resched_if_needed(void *excep_return)
{
	/**
	 * continue iff a rescheduling event happened and
	 * we are not called from another exception
	 */
	if (!need_resched || (((uint32_t)excep_return & 0xf) == 1))
		return;

	svc_handler(0, 0);
}


static uint32_t __wait_evt(int timeout_us, task_id_t resched)
{
	task_ *tsk = __get_current();
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
	while (!(evt = atomic_read_clear(&tsk->events)))
	{
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

	/* set the event bit in the receiver message bitmap */
	atomic_or(&receiver->events, event);

	/* Re-schedule if priorities have changed */
	if (in_interrupt_context()) {
		/* the receiver might run again */
		atomic_or(&tasks_ready, 1 << tskid);
		need_resched = 1;
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


void task_enable_irq(int irq)
{
	CPU_NVIC_EN(irq / 32) = 1 << (irq % 32);
}


void task_disable_irq(int irq)
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


/* Initialize IRQs in the NVIC and set their priorities as defined by the
 * DECLARE_IRQ statements. */
static void __nvic_init_irqs(void)
{
	/* Get the IRQ priorities section from the linker */
	extern struct irq_priority __irqprio[];
	extern struct irq_priority __irqprio_end[];
	int irq_count = __irqprio_end - __irqprio;
	int i;

	/* Mask and clear all pending interrupts */
	for (i = 0; i < 5; i++) {
		CPU_NVIC_DIS(i) = 0xffffffff;
		CPU_NVIC_UNPEND(i) = 0xffffffff;
	}

	/* Re-enable global interrupts in case they're disabled.  On a reboot,
	 * they're already enabled; if we've jumped here from another image,
	 * they're not. */
	interrupt_enable();

	/* Set priorities */
	for (i = 0; i < irq_count; i++) {
		uint8_t irq = __irqprio[i].irq;
		uint8_t prio = __irqprio[i].priority;
		uint32_t prio_shift = irq % 4 * 8 + 5;
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
		/* try to get the lock (set 1 into the lock field) */
		__asm__ __volatile__("   ldrex   %0, [%1]\n"
				     "   teq     %0, #0\n"
				     "   it eq\n"
				     "   strexeq %0, %2, [%1]\n"
				     : "=&r" (value)
				     : "r" (&mtx->lock), "r" (2) : "cc");
		/* "value" is equals to 1 if the store conditional failed,
		 * 2 if somebody else owns the mutex, 0 else.
		 */
		if (value == 2) {
			/* contention on the mutex */
			task_wait_event(0);
		}
	} while (value);

	atomic_clear(&mtx->waiters, id);
}

void mutex_unlock(struct mutex *mtx)
{
	uint32_t waiters;
	task_ *tsk = __get_current();

	__asm__ __volatile__("   ldr     %0, [%2]\n"
			     "   str     %3, [%1]\n"
			     : "=&r" (waiters)
			     : "r" (&mtx->lock), "r" (&mtx->waiters), "r" (0)
			     : "cc");
	while (waiters) {
		task_id_t id = 31 - __builtin_clz(waiters);
		/* somebody is waiting on the mutex */
		task_set_event(id, TASK_EVENT_MUTEX, 0);
		waiters &= ~(1 << id);
	}
	/* Ensure no event is remaining from mutex wake-up */
	atomic_clear(&tsk->events, TASK_EVENT_MUTEX);
}


#ifdef CONFIG_DEBUG


int command_task_info(int argc, char **argv)
{
	int i;

	for (i = 0; i < TASK_ID_COUNT; i++) {
		char is_ready = (tasks_ready & (1<<i)) ? 'R' : ' ';
		uart_printf("%2d %c %-16s events %08x\n", i, is_ready,
		            task_names[i], tasks[i].events);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskinfo, command_task_info);


static int command_task_ready(int argc, char **argv)
{
	if (argc < 2) {
		uart_printf("tasks_ready: 0x%08x\n", tasks_ready);
	} else {
		tasks_ready = strtoi(argv[1], NULL, 16);
		uart_printf("Setting tasks_ready to 0x%08x\n", tasks_ready);
		__schedule(0, 0);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(taskready, command_task_ready);
#endif


int task_pre_init(void)
{
	/* Fill in guard value in scratchpad to prevent stack overflow
	 * detection failure on the first context switch. */
	((task_ *)scratchpad)->guard = GUARD_VALUE;

	/* sanity checks about static task invariants */
	BUILD_ASSERT(TASK_ID_COUNT <= sizeof(unsigned) * 8);
	BUILD_ASSERT(TASK_ID_COUNT < (1 << (sizeof(task_id_t) * 8)));

	/* Initialize IRQs */
	__nvic_init_irqs();

	return EC_SUCCESS;
}
