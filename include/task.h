/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#ifndef __EC_TASK_H
#define __EC_TASK_H

#include "board.h"
#include "common.h"
#include "task_id.h"

/* Task event bitmasks */
#define TASK_EVENT_CUSTOM(x) (x & 0x1fffffff)
#define TASK_EVENT_WAKE   (1 << 29)  /* task_wake() called on task */
#define TASK_EVENT_MUTEX  (1 << 30)  /* Mutex unlocking */
#define TASK_EVENT_TIMER  (1 << 31)  /* Timer expired.  For example,
				      * task_wait_event() timed out before
				      * receiving another event. */

/* Disable CPU interrupt bit. This might break the system so think really hard
 * before using these. There are usually better ways of accomplishing this. */
void interrupt_disable(void);

/* Enable CPU interrupt bit. */
void interrupt_enable(void);

/* Return true if we are in interrupt context. */
inline int in_interrupt_context(void);

/* Set an event for task <tskid> and wake it up if it is higher priority than
 * the current task.
 *
 * event : event bitmap to set (TASK_EVENT_*)
 *
 * If wait!=0, after setting the event, de-schedule the calling task to wait
 * for a response event, then return the bitmap of events which have occured
 * (same as task_wait_event()).  Ignored in interrupt context.
 *
 * If wait==0, returns 0.
 *
 * Can be called both in interrupt context and task context. */
uint32_t task_set_event(task_id_t tskid, uint32_t event, int wait);

/* Wake a task.  This sends it the TASK_EVENT_WAKE event. */
static inline void task_wake(task_id_t tskid)
{
	task_set_event(tskid, TASK_EVENT_WAKE, 0);
}

/* Return the identifier of the task currently running. */
task_id_t task_get_current(void);

/* Return a pointer to the bitmap of events of the task. */
uint32_t *task_get_event_bitmap(task_id_t tsk);

/* Wait for the next event.
 *
 * If one or more events are already pending, returns immediately.  Otherwise,
 * it de-schedules the calling task and wakes up the next one in the priority
 * order.
 *
 * If timeout_us > 0, it also sets a timer to produce the TASK_EVENT_TIMER
 * event after the specified micro-second duration.
 *
 * Returns the bitmap of received events (and clears it atomically). */
uint32_t task_wait_event(int timeout_us);

/* Prints the list of tasks using the command output channel.  This may be
 * called from interrupt level. */
void task_print_list(void);

#ifdef CONFIG_TASK_PROFILING
/* Start tracking an interrupt.
 *
 * This must be called from interrupt context(!) before the interrupt routine
 * is called. */
void task_start_irq_handler(void *excep_return);
#else
#define task_start_irq_handler(excep_return)
#endif

/* Change the task scheduled after returning from the exception.
 *
 * If task_send_event() has been called and has set need_resched flag,
 * re-computes which task is running and eventually swaps the context
 * saved on the process stack to restore the new one at exception exit.
 *
 * This must be called from interrupt context(!) and is designed to be the
 * last call of the interrupt handler. */
void task_resched_if_needed(void *excep_return);

/* Initialize tasks and interrupt controller. */
int task_pre_init(void);

/* Start task scheduling.  Does not normally return. */
int task_start(void);

/* Return non-zero if task_start() has been called and task scheduling has
 * started. */
int task_start_called(void);

/* Enable an interrupt. */
void task_enable_irq(int irq);

/* Disable an interrupt. */
void task_disable_irq(int irq);

/* Software-trigger an interrupt. */
void task_trigger_irq(int irq);

/* Clear a pending interrupt.
 *
 * Note that most interrupts can be removed from the pending state simply by
 * handling whatever caused the interrupt in the first place.  This only needs
 * to be called if an interrupt handler disables itself without clearing the
 * reason for the interrupt, and then the interrupt is re-enabled from a
 * different context. */
void task_clear_pending_irq(int irq);

struct mutex {
	uint32_t lock;
	uint32_t waiters;
};

/* Try to lock the mutex mtx and de-schedule the current task if mtx is already
 * locked by another task.
 *
 * Must not be used in interrupt context! */
void mutex_lock(struct mutex *mtx);

/* Release a mutex previously locked by the same task. */
void mutex_unlock(struct mutex *mtx);

struct irq_priority {
	uint8_t irq;
	uint8_t priority;
};

/* Helper macros to build the IRQ handler name */
#define IRQ_BUILD_NAME(prefix, irqnum, postfix) prefix ## irqnum ## postfix
#define IRQ_HANDLER(irqname)  IRQ_BUILD_NAME(irq_,irqname,_handler)

/* Connects the interrupt handler "routine" to the irq number "irq" and ensures
 * it is enabled in the interrupt controller with the right priority. */
#define DECLARE_IRQ(irq, routine, priority)                     \
	void IRQ_HANDLER(irq)(void)				\
	{							\
		void *ret = __builtin_return_address(0);	\
		task_start_irq_handler(ret);			\
		routine();					\
		task_resched_if_needed(ret);			\
	}							\
	const struct irq_priority IRQ_BUILD_NAME(prio_, irq, )  \
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}

#endif  /* __EC_TASK_H */
