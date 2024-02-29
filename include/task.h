/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Task scheduling / events module for Chrome EC operating system */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_TASK_H
#define __CROS_EC_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "atomic_t.h"
#include "common.h"
#include "compile_time_macros.h"
#include "task_id.h"

#include <stdbool.h>

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 33

/* Task event bitmasks */
/* Tasks may use the bits in TASK_EVENT_CUSTOM_BIT for their own events */
#define TASK_EVENT_CUSTOM_BIT(x) BUILD_CHECK_INLINE(BIT(x), BIT(x) & 0x0ffff)

/* Used to signal that sysjump preparation has completed */
#define TASK_EVENT_SYSJUMP_READY BIT(16)

/* Used to signal that IPC layer is available for sending new data */
#define TASK_EVENT_IPC_READY BIT(17)

#define TASK_EVENT_PD_AWAKE BIT(18)

/* npcx peci event */
#define TASK_EVENT_PECI_DONE BIT(19)

/* I2C tx/rx interrupt handler completion event. */
#ifdef CHIP_STM32
#define TASK_EVENT_I2C_COMPLETION(port) (1 << ((port) + 20))
#define TASK_EVENT_I2C_IDLE (TASK_EVENT_I2C_COMPLETION(0))
#define TASK_EVENT_MAX_I2C 6
#ifdef I2C_PORT_COUNT
#if (I2C_PORT_COUNT > TASK_EVENT_MAX_I2C)
#error "Too many i2c ports for i2c events"
#endif
#endif
#else
#define TASK_EVENT_I2C_IDLE BIT(20)
#define TASK_EVENT_PS2_DONE BIT(21)
#endif

/* DMA transmit complete event */
#define TASK_EVENT_DMA_TC BIT(26)
/* ADC interrupt handler event */
#define TASK_EVENT_ADC_DONE BIT(27)
/*
 * task_reset() that was requested has been completed
 *
 * For test-only builds, may be used by some tasks to restart themselves.
 */
#define TASK_EVENT_RESET_DONE BIT(28)
/* task_wake() called on task */
#define TASK_EVENT_WAKE BIT(29)
/* Mutex unlocking */
#define TASK_EVENT_MUTEX BIT(30)
/*
 * Timer expired.  For example, task_wait_event() timed out before receiving
 * another event.
 */
#define TASK_EVENT_TIMER (1U << 31)

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
 * Check if interrupts are enabled
 */
bool is_interrupt_enabled(void);

/*
 * Define irq_lock and irq_unlock that match the function signatures to Zephyr's
 * functions. In reality, these simply call the current implementation of
 * interrupt_disable() and interrupt_enable().
 */
#ifndef CONFIG_ZEPHYR
/**
 * Perform the same operation as interrupt_disable but allow nesting. The
 * return value from this function should be used as the argument to
 * irq_unlock. Do not attempt to parse the value, it is a representation
 * of the state and not an indication of any form of count.
 *
 * For more information see:
 * https://docs.zephyrproject.org/latest/reference/kernel/other/interrupts.html#c.irq_lock
 *
 * @return Lock key to use for restoring the state via irq_unlock.
 */
uint32_t irq_lock(void);

/**
 * Perform the same operation as interrupt_enable but allow nesting. The key
 * should be the unchanged value returned by irq_lock.
 *
 * For more information see:
 * https://docs.zephyrproject.org/latest/reference/kernel/other/interrupts.html#c.irq_unlock
 *
 * @param key The lock-out key used to restore the interrupt state.
 */
void irq_unlock(uint32_t key);
#endif /* CONFIG_ZEPHYR */

/**
 * Return true if we are in interrupt context.
 */
bool in_interrupt_context(void);

/**
 * Return true if we are in software interrupt context.
 */
bool in_soft_interrupt_context(void);

/**
 * Return current interrupt mask with disabling interrupt. Meaning is
 * chip-specific and should not be examined; just pass it to set_int_mask() to
 * restore a previous interrupt state after interrupt disable.
 */
uint32_t read_clear_int_mask(void);

/**
 * Set interrupt mask. As with interrupt_disable(), use with care.
 */
void set_int_mask(uint32_t val);

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
 */
void task_set_event(task_id_t tskid, uint32_t event);

/**
 * Wake a task.  This sends it the TASK_EVENT_WAKE event.
 *
 * @param tskid		Task to wake
 */
static inline void task_wake(task_id_t tskid)
{
	task_set_event(tskid, TASK_EVENT_WAKE);
}

/**
 * Return the identifier of the task currently running.
 */
task_id_t task_get_current(void);

#ifdef CONFIG_ZEPHYR
/**
 * Check if this current task is running in deferred context
 */
bool in_deferred_context(void);
#else
/* All ECOS deferred calls run from the HOOKS task */
static inline bool in_deferred_context(void)
{
#ifdef HAS_TASK_HOOKS
	return (task_get_current() == TASK_ID_HOOKS);
#else
	return false;
#endif /* HAS_TASK_HOOKS */
}
#endif /* CONFIG_ZEPHYR */

/**
 * Return a pointer to the bitmap of events of the task.
 */
atomic_t *task_get_event_bitmap(task_id_t tskid);

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
 * @return The bitmap of received events.
 */
uint32_t task_wait_event(int timeout_us);

/**
 * Wait for any event included in an event mask.
 *
 * If one or more events are already pending, returns immediately.  Otherwise,
 * it de-schedules the calling task and wakes up the next one in the priority
 * order.  Automatically clears the bitmap of received events before returning
 * the events which are set.
 *
 * @param event_mask	Bitmap of task events to wait for.
 *
 * @param timeout_us	If > 0, sets a timer to produce the TASK_EVENT_TIMER
 *			event after the specified micro-second duration.
 *
 * @return		The bitmap of received events. Includes
 *			TASK_EVENT_TIMER if the timeout is reached.
 */
uint32_t task_wait_event_mask(uint32_t event_mask, int timeout_us);

/**
 * Prints the list of tasks.
 *
 * Uses the command output channel.  May be called from interrupt level.
 */
void task_print_list(void);

/**
 * Returns the name of the task.
 */
const char *task_get_name(task_id_t tskid);

#ifdef CONFIG_TASK_PROFILING
/**
 * Start tracking an interrupt.
 *
 * This must be called from interrupt context (!) before the interrupt routine
 * is called.
 */
void task_start_irq_handler(void *excep_return);
void task_end_irq_handler(void *excep_return);
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

#ifdef CONFIG_FPU
/**
 * Clear floating-point used flag for currently executing task. This means the
 * FPU regs will not be stored on context switches until the next time floating
 * point is used for currently executing task.
 */
void task_clear_fp_used(void);
#endif

/**
 * Mark all tasks as ready to run and reschedule the highest priority task.
 */
void task_enable_all_tasks(void);

/**
 * Enable a task.
 */
void task_enable_task(task_id_t tskid);

/**
 * Task is enabled.
 */
bool task_enabled(task_id_t tskid);

/**
 * Disable a task.
 *
 * If the task disable itself, this will cause an immediate reschedule.
 */
void task_disable_task(task_id_t tskid);

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

/*
 * A task that supports resets may call this to indicate that it may be reset
 * at any point between this call and the next call to task_disable_resets().
 *
 * Calling this function will trigger any resets that were requested while
 * resets were disabled.
 *
 * It is not expected for this to be called if resets are already enabled.
 */
void task_enable_resets(void);

/*
 * A task that supports resets may call this to indicate that it may not be
 * reset until the next call to task_enable_resets(). Any calls to task_reset()
 * during this time will cause a reset request to be queued, and executed
 * the next time task_enable_resets() is called.
 *
 * Must not be called if resets are already disabled.
 */
void task_disable_resets(void);

/*
 * If the current task was reset, completes the reset operation.
 *
 * Returns a non-zero value if the task was reset; tasks with state outside
 * of the stack should perform any necessary cleanup immediately after calling
 * this function.
 *
 * Tasks that support reset must call this function once at startup before
 * doing anything else.
 *
 * Must only be called once at task startup.
 */
int task_reset_cleanup(void);

/*
 * Resets the specified task, which must not be the current task,
 * to initial state.
 *
 * Returns EC_SUCCESS, or EC_ERROR_INVAL if the specified task does
 * not support resets.
 *
 * If wait is true, blocks until the task has been reset. Otherwise,
 * returns immediately - in this case the task reset may be delayed until
 * that task can be safely reset. The duration of this delay depends on the
 * task implementation.
 */
int task_reset(task_id_t id, int wait);

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

/**
 * Check if irq is pending.
 *
 * Returns true if interrupt with given number is pending, false otherwise.
 */
bool task_is_irq_pending(int irq);

#ifdef CONFIG_ZEPHYR
typedef struct k_mutex mutex_t;

#define mutex_lock(mtx) (k_mutex_lock(mtx, K_FOREVER))
#define mutex_unlock(mtx) (k_mutex_unlock(mtx))
#else
struct mutex {
	uint32_t lock;
	atomic_t waiters;
};

typedef struct mutex mutex_t;

/**
 * K_MUTEX_DEFINE is a macro normally provided by the Zephyr kernel,
 * and allows creation of a static mutex without the need to
 * initialize it.  We provide the same macro for CrOS EC OS so that we
 * can use it in shared code.
 */
#define K_MUTEX_DEFINE(name) mutex_t name = {}

/**
 * Lock a mutex.
 *
 * This tries to lock the mutex mtx.  If the mutex is already locked by another
 * task, de-schedules the current task until the mutex is again unlocked.
 *
 * Must not be used in interrupt context!
 */
void mutex_lock(mutex_t *mtx);

/**
 * Attempt to lock a mutex
 *
 * This tries to lock the mutex mtx. If the mutex is already locked by another
 * thread this function returns 0. If the mutex is unlocked, lock the mutex and
 * return 1.
 *
 * Must not be used in interrupt context!
 */
int mutex_try_lock(mutex_t *mtx);

/**
 * Release a mutex previously locked by the same task.
 */
void mutex_unlock(mutex_t *mtx);

/** Zephyr will try to init the mutex using `k_mutex_init()`. */
#define k_mutex_init(mutex) 0
#endif /* CONFIG_ZEPHYR */

struct irq_priority {
	uint8_t irq;
	uint8_t priority;
};

/*
 * Some cores may make use of this struct for mapping irqs to handlers
 * for DECLARE_IRQ in a linker-script defined section.
 */
struct irq_def {
	int irq;

	/* The routine which was declared as an IRQ */
	void (*routine)(void);

	/*
	 * The routine usually needs wrapped so the core can handle it
	 * as an IRQ.
	 */
	void (*handler)(void);
};

/*
 * Implement the DECLARE_IRQ(irq, routine, priority) macro which is
 * a core specific helper macro to declare an interrupt handler "routine".
 */
#ifndef CONFIG_ZEPHYR
#ifdef CONFIG_COMMON_RUNTIME
#include "irq_handler.h"
#else
#define IRQ_HANDLER(irqname) CONCAT3(irq_, irqname, _handler)
#define IRQ_HANDLER_OPT(irqname) CONCAT3(irq_, irqname, _handler_optional)
#define DECLARE_IRQ(irq, routine, priority) DECLARE_IRQ_(irq, routine, priority)
#define DECLARE_IRQ_(irq, routine, priority) \
	static void __keep routine(void);    \
	void IRQ_HANDLER_OPT(irq)(void) __attribute__((alias(#routine)))

/* Include ec.irqlist here for compilation dependency */
#define ENABLE_IRQ(x)
#if !defined(CONFIG_DFU_BOOTMANAGER_MAIN)
#include "ec.irqlist"
#endif /* !defined(CONFIG_DFU_BOOTMANAGER_MAIN) */
#endif /* CONFIG_COMMON_RUNTIME */
#endif /* !CONFIG_ZEPHYR */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TASK_H */
