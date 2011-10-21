/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Operating system objects for EC */

#ifndef __CROS_EC_OS_H
#define __CROS_EC_OS_H

#include "ec_common.h"
#include "ec_os_types.h"

/* Timeout value which means "wait forever". */
#define EC_OS_FOREVER -1

/*****************************************************************************/
/* Tasks */

/* Creates a task of the specified priority and stack size.  If
 * stack_size=0, uses the default stack size.  The task will call
 * task_func(param).  Fills <task>. */
EcError EcTaskCreate(EcTask* task, int priority, int stack_size,
                     void (*task_func)(void*), void* param);

/* Sleep for the specified number of microseconds. */
void EcTaskSleep(int usec);

/* Exits the current task. */
void EcTaskExit(void);

/*****************************************************************************/
/* Software interrupts (SWI) */

/* Creates a SWI of the specified priority.  When the SWI is
 * triggered, it will call swi_func(param, bits), where <bits> is the
 * accumulated bits value from all preceding calls to EcSwiPost().
 * Fills <swi>. */
EcError EcSwiCreate(EcSwi* swi, int priority,
                    void (*swi_func)(void*, uint32_t), void* param);

/* Sets the specified bits in the SWI. */
EcError EcSwiPost(EcSwi* swi, uint32_t bits);

/*****************************************************************************/
/* Timers */

/* Timer flags */
/* Timer is periodic; if not present, timer is one-shot. */
#define EC_TIMER_FLAG_PERIODIC   0x01
#define EC_TIMER_FLAG_STARTED    0x02

/* Creates a timer which will call timer_func(param) after the
 * specified interval.  See EC_TIMER_FLAG_* for valid flags.  Fills
 * <timer>. */
EcError EcTimerCreate(EcTimer* timer, int interval_usec, int priority,
                      uint32_t flags, void (*timer_func)(void*), void* param);

/* Stops a timer. */
EcError EcTimerStop(EcTimer* timer);

/* Starts a timer. */
EcError EcTimerStart(EcTimer* timer);

/*****************************************************************************/
/* Semaphores */

/* Creates a semaphore with the specified initial count.  Fills <semaphore>. */
EcError EcSemaphoreCreate(EcSemaphore* semaphore, int initial_count);

/* Posts the semaphore, incrementing its count by one.  If count>0,
 * this will allow the next task pending on the semaphore to run. */
EcError EcSemaphorePost(EcSemaphore* semaphore);

/* Waits up to <timeout_usec> microseconds (or forever, if
 * timeout_usec==EC_OS_FOREVER) for the semaphore.  If it's unable to
 * acquire the semaphore before the timeout, returns
 * EC_ERROR_TIMEOUT. */
EcError EcSemaphoreWait(EcSemaphore* semaphore, int timeout_usec);

/* Stores the current semaphore count into <count_ptr>. */
EcError EcSemaphoreGetCount(EcSemaphore* semaphore, int* count_ptr);

/*****************************************************************************/
/* Events
 *
 * To be compatible with all platforms, only one task at a time may
 * wait on an event. */

/* Creates an event with the specified initial bits.  Fills <event>. */
EcError EcEventCreate(EcEvent* event, uint32_t initial_bits);

/* Turns on the specified bits in the event. */
EcError EcEventPost(EcEvent* event, uint32_t bits);

/* Waits up to <timeout_usec> microseconds (or forever, if
 * timeout_usec==EC_OS_FOREVER) for all of the requested bits to be
 * set in the event.  Returns EC_ERROR_TIMEOUT on timeout. */
EcError EcEventWaitAll(EcEvent* event, uint32_t bits, int timeout_usec);

/* Waits up to <timeout_usec> microseconds (or forever, if
 * timeout_usec==EC_OS_FOREVER) for any of the requested bits to be
 * set in the event.  If got_bits_ptr!=NULL, sets it to the bits which
 * were posted, and clears those bits.  Returns EC_ERROR_TIMEOUT on timeout. */
EcError EcEventWaitAny(EcEvent* event, uint32_t bits, uint32_t* got_bits_ptr,
                       int timeout_usec);

/*****************************************************************************/
/* Other OS functions */

/* Initializes the OS.  Must be called before any of the functions above. */
void EcOsInit(void);

/* Starts OS task management.  Returns when all threads have exited.
 * This function should be called by main(). */
void EcOsStart(void);


#endif
