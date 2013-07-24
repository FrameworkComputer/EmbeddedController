/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#ifndef __CROS_EC_TASK_H
#define __CROS_EC_TASK_H

#include "common.h"
#include "task_id.h"

/* Task event bitmasks */
/* Tasks may use the bits in TASK_EVENT_CUSTOM for their own events */
#define TASK_EVENT_CUSTOM(x)	(x & 0x0fffffff)
/* I2C interrupt handler event */
#define TASK_EVENT_I2C_IDLE	(1 << 28)
/* task_wake() called on task */
#define TASK_EVENT_WAKE		(1 << 29)
/* Mutex unlocking */
#define TASK_EVENT_MUTEX	(1 << 30)
/*
 * Timer expired.  For example, task_wait_event() timed out before receiving
 * another event.
 */
#define TASK_EVENT_TIMER	(1U << 31)

/* Maximum time for task_wait_event() */
#define TASK_MAX_WAIT_US 0x7fffffff

/**
 * Disable CPU interrupt bit.
 *
 * This might break the system so think really hard before using these. There
 * are usually better ways of accomplishing this.
 */
void interrupt_disable(void);

/**
 * Enable CPU interrupt bit.
 */
void interrupt_enable(void);

/**
 * Return true if we are in interrupt context.
 */
inline int in_interrupt_context(void);

/**
 * Set a task event.
 *
 * If the task is higher priority than the current task, this will cause an
 * immediate context switch to the new task.
 *
 * Can be called both in interrupt context and task context.
 *
 * @param tskid		Task to set event for
 * @param event		Event bitmap to set (TASK_EVENT_*)
 * @param wait		If non-zero, after setting the event, de-schedule the
 *			calling task to wait for a response event.  Ignored in
 *			interrupt context.
 * @return		The bitmap of events which occurred if wait!=0, else 0.
 */
uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait);

/**
 * Wake a task.  This sends it the TASK_EVENT_WAKE event.
 *
 * @param tskid		Task to wake
 */
static inline void task_wake(task_id_t tskid)
{
	task_set_event(tskid, TASK_EVENT_WAKE, 0);
}

/**
 * Return the identifier of the task currently running.
 */
task_id_t task_get_current(void);

/**
 * Return a pointer to the bitmap of events of the task.
 */
uint32_t *task_get_event_bitmap(task_id_t tskid);

/**
 * Wait for the next event.
 *
 * If one or more events are already pending, returns immediately.  Otherwise,
 * it de-schedules the calling task and wakes up the next one in the priority
 * order.  Automatically clears the bitmap of received events before returning
 * the events which are set.
 *
 * @param timeout_us	If > 0, sets a timer to produce the TASK_EVENT_TIMER
 *			event after the specified micro-second duration.
 *
 * @return The bitmap of received events. */
uint32_t task_wait_event(int timeout_us);

/**
 * Prints the list of tasks.
 *
 * Uses the command output channel.  May be called from interrupt level.
 */
void task_print_list(void);

#ifdef CONFIG_TASK_PROFILING
/**
 * Start tracking an interrupt.
 *
 * This must be called from interrupt context (!) before the interrupt routine
 * is called.
 */
void task_start_irq_handler(void *excep_return);
#else
#define task_start_irq_handler(excep_return)
#endif

/**
 * Change the task scheduled to run after returning from the exception.
 *
 * If task_send_event() has been called and has set need_resched flag,
 * re-computes which task is running and eventually swaps the context
 * saved on the process stack to restore the new one at exception exit.
 *
 * This must be called from interrupt context (!) and is designed to be the
 * last call of the interrupt handler.
 */
void task_resched_if_needed(void *excep_return);

/**
 * Initialize tasks and interrupt controller.
 */
void task_pre_init(void);

/**
 * Start task scheduling.  Does not normally return.
 */
int task_start(void);

/**
 * Return non-zero if task_start() has been called and task scheduling has
 * started.
 */
int task_start_called(void);

/**
 * Enable an interrupt.
 */
void task_enable_irq(int irq);

/**
 * Disable an interrupt.
 */
void task_disable_irq(int irq);

/**
 * Software-trigger an interrupt.
 */
void task_trigger_irq(int irq);

/**
 * Clear a pending interrupt.
 *
 * Note that most interrupts can be removed from the pending state simply by
 * handling whatever caused the interrupt in the first place.  This only needs
 * to be called if an interrupt handler disables itself without clearing the
 * reason for the interrupt, and then the interrupt is re-enabled from a
 * different context.
 */
void task_clear_pending_irq(int irq);

struct mutex {
	uint32_t lock;
	uint32_t waiters;
};

/**
 * Lock a mutex.
 *
 * This tries to lock the mutex mtx.  If the mutex is already locked by another
 * task, de-schedules the current task until the mutex is again unlocked.
 *
 * Must not be used in interrupt context!
 */
void mutex_lock(struct mutex *mtx);

/**
 * Release a mutex previously locked by the same task.
 */
void mutex_unlock(struct mutex *mtx);

struct irq_priority {
	uint8_t irq;
	uint8_t priority;
};

/* Helper macros to build the IRQ handler and priority struct names */
#define IRQ_HANDLER(irqname) CONCAT3(irq_, irqname, _handler)
#define IRQ_PRIORITY(irqname) CONCAT2(prio_, irqname)
/*
 * Macro to connect the interrupt handler "routine" to the irq number "irq" and
 * ensure it is enabled in the interrupt controller with the right priority.
 */
#define DECLARE_IRQ(irq, routine, priority)                     \
	void IRQ_HANDLER(irq)(void)				\
	{							\
		void *ret = __builtin_return_address(0);	\
		task_start_irq_handler(ret);			\
		routine();					\
		task_resched_if_needed(ret);			\
	}							\
	const struct irq_priority IRQ_PRIORITY(irq)		\
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}

#endif  /* __CROS_EC_TASK_H */
