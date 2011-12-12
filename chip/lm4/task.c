/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#include <stdint.h>

#include "config.h"
#include "atomic.h"
#include "console.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "registers.h"
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
		uint8_t stack[0];  /* task stack */
	};
	uint32_t context[TASK_SIZE/4];
} task_;

/* declare task routine prototypes */
#define TASK(n, r, d) int r(void *);
#include TASK_LIST
void __idle(void);
CONFIG_TASK_LIST
#undef TASK


extern void __switchto(task_ *from, task_ *to);


/* declare and fill the contexts for all the tasks */
#define TASK(n, r, d)  {						\
	.context[0] = (uint32_t)(tasks + TASK_ID_##n + 1) - 64,	        \
	.context[TASK_SIZE/4 - 8/*r0*/] = (uint32_t)d,                  \
	/* TODO set a LR to a trap */		                        \
	.context[TASK_SIZE/4 - 2/*pc*/] = (uint32_t)r,                  \
	.context[TASK_SIZE/4 - 1/*psr*/] = 0x01000000 },
#include TASK_LIST
static task_ tasks[] __attribute__((section(".data.tasks")))
		__attribute__((aligned(TASK_SIZE))) = {
	TASK(IDLE, __idle, 0)
	CONFIG_TASK_LIST
};
#undef TASK
/* reserve space to discard context on first context switch */
uint32_t scratchpad[17] __attribute__((section(".data.tasks")));

/* context switch at the next exception exit if needed */
/* TODO: who sets this back to 0 after it's set to 1? */
static int need_resched = 0;

/**
 * bitmap of all tasks ready to be run
 *
 * Currently all tasks are enabled at startup.
 */
static unsigned tasks_ready = (1<<TASK_ID_COUNT) - 1;


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
	uint32_t reg;

	/* push the priority to -1 until the return, to avoid being
	 * interrupted */
	asm volatile("mov %0, #1\n"
	             "msr faultmask, %0" :"=r"(reg));
	current = __get_task_scheduled();
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


/**
 * Change the task scheduled after returning from the exception.
 *
 * If task_send_msg has been called and has set need_resched flag,
 * we re-compute which task is running and eventually swap the context
 * saved on the process stack to restore the new one at exception exit.
 *
 * it must be called from interrupt context !
 */
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


static uint32_t __wait_msg(int timeout_us, task_id_t resched)
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


uint32_t task_send_msg(task_id_t tskid, task_id_t from, int wait)
{
	task_ *receiver = __task_id_to_ptr(tskid);
	ASSERT(receiver);

	if (from == TASK_ID_CURRENT) {
		from = task_get_current();
	}

	/* set the event bit in the receiver message bitmap */
	atomic_or(&receiver->events, 1 << from);

	/* Re-schedule if priorities have changed */
	if (in_interrupt_context()) {
		/* the receiver might run again */
		tasks_ready |= 1 << tskid;
		need_resched = 1;
	} else {
		if (wait)
			return __wait_msg(-1, tskid);
		else
			__schedule(0, tskid);
	}

	return 0;
}


uint32_t task_wait_msg(int timeout_us)
{
	return __wait_msg(timeout_us, TASK_ID_IDLE);
}


void task_enable_irq(int irq)
{
	LM4_NVIC_EN(irq / 32) = 1 << (irq % 32);
}


void task_disable_irq(int irq)
{
	LM4_NVIC_DIS(irq / 32) = 1 << (irq % 32);
}


void task_trigger_irq(int irq)
{
	LM4_NVIC_SWTRIG = irq;
}


/**
 * Enable all used IRQ in the NVIC and set their priorities
 * as defined by the DECLARE_IRQ statements
 */
static void __nvic_init_irqs(void)
{
	/* get the IRQ priorities section from the linker */
	extern struct irq_priority __irqprio[];
	extern struct irq_priority __irqprio_end[];
	int irq_count = __irqprio_end - __irqprio;
	int i;

	for (i = 0; i < irq_count; i++) {
		uint8_t irq = __irqprio[i].irq;
		uint8_t prio = __irqprio[i].priority;
		uint32_t prio_shift = irq % 4 * 8 + 5;
		LM4_NVIC_PRI(irq / 4) =
				(LM4_NVIC_PRI(irq / 4) &
				 ~(0x7 << prio_shift)) |
				(prio << prio_shift);

		/* TODO: enabling all interrupts here causes a race condition
		   between an interrupt and setting up the handler for it. */
		task_enable_irq(irq);
	}
}


#ifdef CONFIG_DEBUG

/* store the task names for easier debugging */
#define TASK(n, r, d)  #n,
#include TASK_LIST
static const char * const task_names[] = {
	"<< idle >>",
	CONFIG_TASK_LIST
};
#undef TASK


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


static const struct console_command task_commands[] = {
	{"taskinfo", command_task_info},
	{"taskready", command_task_ready}
};
static const struct console_group task_group = {
	"Task", task_commands, ARRAY_SIZE(task_commands)
};
#endif


int task_init(void)
{
	/* sanity checks about static task invariants */
	BUILD_ASSERT(TASK_ID_COUNT <= sizeof(unsigned) * 8);
	BUILD_ASSERT(TASK_ID_COUNT < (1 << (sizeof(task_id_t) * 8)));

	/* Initialize IRQs */
	__nvic_init_irqs();

#ifdef CONFIG_DEBUG
	/* Register our internal commands */
	console_register_commands(&task_group);
#endif

	return EC_SUCCESS;
}
