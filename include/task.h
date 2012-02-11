/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

#ifndef __EC_TASK_H
#define __EC_TASK_H

#include "common.h"
#include "task_id.h"

/* Disables CPU interrupt bit. This might break the system so think really hard
 * before using these. There are usually better ways of accomplishing this. */
void interrupt_disable(void);

/* Enables CPU interrupt */
void interrupt_enable(void);

/**
 * Return true if we are in interrupt context
 */
inline int in_interrupt_context(void);

/**
 * Send a message to a task and wake it up if it is higher priority than us
 *
 * tskid : identifier of the receiver task
 * from : identifier of the sender of the message
 * wait : after sending, de-schedule the calling task to wait for the answer
 *
 * returns the bitmap of events which have occured.
 *
 * Can be called both in interrupt context and task context.
 */
uint32_t task_send_msg(task_id_t tskid, task_id_t from, int wait);

/**
 * Return the identifier of the task currently running
 *
 * when called in interrupt context, returns TASK_ID_INVALID
 */
task_id_t task_get_current(void);

/**
 * Return a pointer to the bitmap of received events of the task.
 */
uint32_t *task_get_event_bitmap(task_id_t tsk);

/**
 * Waits for the incoming next message.
 *
 * if an event is already pending, it returns it immediatly, else it
 * de-schedules the calling task and wake up the next one in the priority order
 *
 * if timeout_us > 0, it also sets a timer to produce an event after the
 * specified micro-second duration.
 *
 * returns the bitmap of received events (and clear it atomically).
 */
uint32_t task_wait_msg(int timeout_us);

/**
 * Changes the task scheduled after returning from the exception.
 *
 * If task_send_msg has been called and has set need_resched flag,
 * we re-compute which task is running and eventually swap the context
 * saved on the process stack to restore the new one at exception exit.
 *
 * it must be called from interrupt context !
 * and it is designed to be the last call of the interrupt handler.
 */
void task_resched_if_needed(void *excep_return);

/* Initializes tasks and interrupt controller. */
int task_init(void);

/* Starts task scheduling. */
int task_start(void);

/* Enables an interrupt. */
void task_enable_irq(int irq);

/* Disables an interrupt. */
void task_disable_irq(int irq);

/* Software-triggers an interrupt. */
void task_trigger_irq(int irq);

struct mutex {
	uint32_t lock;
	uint32_t waiters;
};

/**
 * try to lock the mutex mtx
 * and de-schedule the task if it is already locked by another task.
 *
 * Should not be used in interrupt context !
 */
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

/**
 * Connects the interrupt handler "routine" to the irq number "irq" and
 * ensures it is enabled in the interrupt controller with the right priority.
 */
#define DECLARE_IRQ(irq, routine, priority)                     \
	void IRQ_HANDLER(irq)(void)				\
	{							\
		void *ret = __builtin_return_address(0);	\
		routine();					\
		task_resched_if_needed(ret);			\
	}							\
	const struct irq_priority IRQ_BUILD_NAME(prio_, irq, )  \
	__attribute__((section(".rodata.irqprio")))		\
			= {irq, priority}

#endif  /* __EC_TASK_H */
