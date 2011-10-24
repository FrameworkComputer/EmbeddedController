/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Operating system library EC */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ec_common.h"
#include "ec_os.h"


static int os_has_started = 0;
static pthread_mutex_t os_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t os_start_cond = PTHREAD_COND_INITIALIZER;


/* Waits for OS to start */
static void WaitForOsStart(void) {
  pthread_mutex_lock(&os_start_mutex);
  if (!os_has_started)
    pthread_cond_wait(&os_start_cond, &os_start_mutex);
  pthread_mutex_unlock(&os_start_mutex);
}

static void UsecToTimespec(int usec, struct timespec* ts) {
  ts->tv_sec = usec / 1000000;
  ts->tv_nsec = 1000 * (long)(usec % 1000000);
}

/*****************************************************************************/
/* Tasks */

/* Internal data for a task */
typedef struct EcTaskInternal {
  pthread_t thread;
  void (*task_func)(void*);
  void* param;
  struct EcTaskInternal* next;
} EcTaskInternal;


static EcTaskInternal* task_list = NULL;
static pthread_mutex_t task_list_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Task wrapper.  Waits for OS to start, then runs the task function. */
static void* EcTaskWrapper(void* param) {
  EcTaskInternal* ti = (EcTaskInternal*)param;

  WaitForOsStart();

  /* Chain to the task function */
  ti->task_func(ti->param);

  return NULL;
}


EcError EcTaskCreate(EcTask* task, int priority, int stack_size,
                     void (*task_func)(void*), void* param) {
  EcTaskInternal* ti = (EcTaskInternal*)task;
  pthread_attr_t attr;

  /* TODO: priority */

  /* Initialize task data */
  ti->task_func = task_func;
  ti->param = param;

  /* Add it to the task list */
  pthread_mutex_lock(&task_list_mutex);
  ti->next = task_list;
  task_list = ti;
  pthread_mutex_unlock(&task_list_mutex);

  /* Mark thread as joinable */
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  /* Create the thread */
  if (pthread_create(&ti->thread, &attr, EcTaskWrapper, ti) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}


void EcTaskSleep(int usec) {
  usleep(usec);
}


void EcTaskExit(void) {
  pthread_exit(NULL);
}


/*****************************************************************************/
/* Software interrupts (SWI)
 *
 * SWIs don't exist in pthreads.  Simulate them with a thread which waits
 * on a semaphore and calls the SWI function when it wakes. */

typedef struct EcSwiInternal {
  pthread_t thread;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  uint32_t pending_bits;
  void (*swi_func)(void *, uint32_t);
  void* param;
} EcSwiInternal;


/* SWI wrapper.  Loops and calls SWI function when semaphore is signalled. */
static void* EcSwiWrapper(void* param) {
  EcSwiInternal* si = (EcSwiInternal*)param;

  WaitForOsStart();

  while (1) {
    int bits;

    pthread_mutex_lock(&si->mutex);
    pthread_cond_wait(&si->cond, &si->mutex);
    bits = si->pending_bits;
    si->pending_bits = 0;
    pthread_mutex_unlock(&si->mutex);

    if (bits)
      si->swi_func(si->param, bits);
  }

  return NULL;
}


EcError EcSwiCreate(EcSwi* swi, int priority,
                    void (*swi_func)(void*, uint32_t), void* param) {
  EcSwiInternal* si = (EcSwiInternal*)swi;

  /* TODO: priority */

  /* Init internal data */
  si->pending_bits = 0;
  si->swi_func = swi_func;
  si->param = param;

  /* Allocate pthreads objects for the swi */
  if (pthread_mutex_init(&si->mutex, NULL) != 0)
    return EC_ERROR_UNKNOWN;
  if (pthread_cond_init(&si->cond, NULL) != 0)
    return EC_ERROR_UNKNOWN;
  if (pthread_create(&si->thread, NULL, EcSwiWrapper, si) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}


/* Sets the specified bits in the SWI. */
EcError EcSwiPost(EcSwi* swi, uint32_t bits) {
  EcSwiInternal* si = (EcSwiInternal*)swi;
  int prev_bits;

  pthread_mutex_lock(&si->mutex);

  prev_bits = si->pending_bits;
  si->pending_bits |= bits;

  if (!prev_bits)
    pthread_cond_signal(&si->cond);

  pthread_mutex_unlock(&si->mutex);

  return EC_SUCCESS;
}


/*****************************************************************************/
/* Timers */


typedef struct EcTimerInternal {
  pthread_t thread;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  int interval_usec;
  int flags;
  void (*timer_func)(void *);
  void* param;
} EcTimerInternal;

/* Timer flags */
/* Timer is periodic; if not present, timer is one-shot. */
#define EC_TIMER_FLAG_PERIODIC   0x01
#define EC_TIMER_FLAG_STARTED    0x02


/* Timer wrapper.  Loops and calls timer function. */
static void* EcTimerWrapper(void* param) {
  EcTimerInternal* ti = (EcTimerInternal*)param;

  WaitForOsStart();

  while (1) {

    /* Wait for timer to be enabled */
    pthread_mutex_lock(&ti->mutex);
    if (!(ti->flags & EC_TIMER_FLAG_STARTED))
      pthread_cond_wait(&ti->cond, &ti->mutex);
    pthread_mutex_unlock(&ti->mutex);

    /* TODO: should really sleep for interval, less the time used by
     * the previous call.  Or we could use a second thread to
     * pthread_cond_signal() and then immediately go back to sleep. */
    usleep(ti->interval_usec);

    /* Only call the timer func if the flag is still started */
    if (ti->flags & EC_TIMER_FLAG_STARTED)
      ti->timer_func(ti->param);

    if (!(ti->flags & EC_TIMER_FLAG_PERIODIC))
      break;  /* One-shot timer */
  }

  return NULL;
}


/* Creates a timer which will call timer_func(param) after the
 * specified interval.  See EC_TIMER_FLAG_* for valid flags.  Fills
 * <timer>. */
EcError EcTimerCreate(EcTimer* timer, int interval_usec, int priority,
                      uint32_t flags, void (*timer_func)(void*), void* param) {
  EcTimerInternal* ti = (EcTimerInternal*)timer;

  /* TODO: priority */

  /* Init internal data */
  ti->interval_usec = interval_usec;
  ti->flags = flags;
  ti->timer_func = timer_func;
  ti->param = param;

  /* Create thread to call timer func */
  if (pthread_mutex_init(&ti->mutex, NULL) != 0)
    return EC_ERROR_UNKNOWN;
  if (pthread_cond_init(&ti->cond, NULL) != 0)
    return EC_ERROR_UNKNOWN;
  if (pthread_create(&ti->thread, NULL, EcTimerWrapper, ti) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}


/* Stops a timer. */
EcError EcTimerStop(EcTimer* timer) {
  EcTimerInternal* ti = (EcTimerInternal*)timer;

  pthread_mutex_lock(&ti->mutex);
  ti->flags &= ~EC_TIMER_FLAG_STARTED;
  pthread_mutex_unlock(&ti->mutex);
  return EC_SUCCESS;
}


/* Starts a timer. */
EcError EcTimerStart(EcTimer* timer) {
  EcTimerInternal* ti = (EcTimerInternal*)timer;

  pthread_mutex_lock(&ti->mutex);
  ti->flags |= EC_TIMER_FLAG_STARTED;
  pthread_cond_signal(&ti->cond);
  pthread_mutex_unlock(&ti->mutex);
  return EC_SUCCESS;
}


/*****************************************************************************/
/* Semaphores */

typedef struct EcSemaphoreInternal {
  sem_t sem;
} EcSemaphoreInternal;


EcError EcSemaphoreCreate(EcSemaphore* semaphore, int initial_count) {
  EcSemaphoreInternal* si = (EcSemaphoreInternal*)semaphore;

  if (sem_init(&si->sem, 0, initial_count) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}


EcError EcSemaphorePost(EcSemaphore* semaphore) {
  EcSemaphoreInternal* si = (EcSemaphoreInternal*)semaphore;

  if (sem_post(&si->sem) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}


EcError EcSemaphoreWait(EcSemaphore* semaphore, int timeout_usec) {
  EcSemaphoreInternal* si = (EcSemaphoreInternal*)semaphore;
  int rv;

  if (timeout_usec == 0) {
    rv = sem_trywait(&si->sem);

  } else if (timeout_usec == EC_OS_FOREVER) {
    rv = sem_wait(&si->sem);
    if (errno == EAGAIN)
      return EC_ERROR_TIMEOUT;

  } else {
    struct timespec ts;
    UsecToTimespec(timeout_usec, &ts);
    rv = sem_timedwait(&si->sem, &ts);
    if (errno == ETIMEDOUT)
      return EC_ERROR_TIMEOUT;
  }

  return (rv == 0 ? EC_SUCCESS : EC_ERROR_UNKNOWN);
}


EcError EcSemaphoreGetCount(EcSemaphore* semaphore, int* count_ptr) {
  EcSemaphoreInternal* si = (EcSemaphoreInternal*)semaphore;

  if (sem_getvalue(&si->sem, count_ptr) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}

/*****************************************************************************/
/* Events */

typedef struct EcEventInternal {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  uint32_t bits_set;
  uint32_t bits_or;
  uint32_t bits_and;
} EcEventInternal;

EcError EcEventCreate(EcEvent* event, uint32_t initial_bits) {
  EcEventInternal* ei = (EcEventInternal*)event;

  /* Init internal data */
  ei->bits_set = initial_bits;
  ei->bits_or = ei->bits_and = 0;
  if (pthread_mutex_init(&ei->mutex, NULL) != 0)
    return EC_ERROR_UNKNOWN;
  if (pthread_cond_init(&ei->cond, NULL) != 0)
    return EC_ERROR_UNKNOWN;

  return EC_SUCCESS;
}

/* Turns on the specified bits in the event. */
EcError EcEventPost(EcEvent* event, uint32_t bits) {
  EcEventInternal* ei = (EcEventInternal*)event;

  pthread_mutex_lock(&ei->mutex);

  ei->bits_set |= bits;

  /* See if that's enough bits to release the thread waiting on us */
  if (ei->bits_or & ei->bits_set) {
    ei->bits_or = 0;
    pthread_cond_signal(&ei->cond);

  } else if (ei->bits_and && (ei->bits_and & ei->bits_set) == ei->bits_and) {
    ei->bits_and = 0;
    pthread_cond_signal(&ei->cond);
  }

  pthread_mutex_unlock(&ei->mutex);
  return EC_SUCCESS;
}


EcError EcEventWaitAll(EcEvent* event, uint32_t bits, int timeout_usec) {
  EcEventInternal* ei = (EcEventInternal*)event;
  int rv = 0;

  pthread_mutex_lock(&ei->mutex);

  /* Only wait if we don't have the bits we need */
  if ((ei->bits_set & bits) != bits) {
    ei->bits_and = bits;

    if (timeout_usec == EC_OS_FOREVER) {
      rv = pthread_cond_wait(&ei->cond, &ei->mutex);
    } else {
      struct timespec ts;
      UsecToTimespec(timeout_usec, &ts);
      rv = pthread_cond_timedwait(&ei->cond, &ei->mutex, &ts);
    }
  }

  /* If we succeeded, consume all the bits we waited for */
  if (!rv)
    ei->bits_set &= ~bits;

  pthread_mutex_unlock(&ei->mutex);

  if (rv == ETIMEDOUT)
    return EC_ERROR_TIMEOUT;
  else
    return (rv == 0 ? EC_SUCCESS : EC_ERROR_UNKNOWN);
}


EcError EcEventWaitAny(EcEvent* event, uint32_t bits, uint32_t* got_bits_ptr,
                       int timeout_usec) {
  EcEventInternal* ei = (EcEventInternal*)event;
  int rv = 0;

  pthread_mutex_lock(&ei->mutex);

  /* Only wait if we don't have the bits we need */
  if (!(ei->bits_set & bits)) {
    ei->bits_or = bits;

    if (timeout_usec == EC_OS_FOREVER) {
      rv = pthread_cond_wait(&ei->cond, &ei->mutex);
    } else {
      struct timespec ts;
      UsecToTimespec(timeout_usec, &ts);
      rv = pthread_cond_timedwait(&ei->cond, &ei->mutex, &ts);
    }
  }

  /* If we succeeded, consume all the bits we waited for */
  if (!rv) {
    if (got_bits_ptr)
      *got_bits_ptr = ei->bits_set & bits;
    ei->bits_set &= ~bits;
  }

  pthread_mutex_unlock(&ei->mutex);

  if (rv == ETIMEDOUT)
    return EC_ERROR_TIMEOUT;
  else
    return (rv == 0 ? EC_SUCCESS : EC_ERROR_UNKNOWN);
}

/*****************************************************************************/
/* Other functions */


void EcOsInit(void) {

  /* Make sure struct sizes are correct */
  //printf("%ld", sizeof(EcTimerInternal));
  assert(EC_TASK_STRUCT_SIZE == sizeof(EcTaskInternal));
  assert(EC_SWI_STRUCT_SIZE == sizeof(EcSwiInternal));
  assert(EC_TIMER_STRUCT_SIZE == sizeof(EcTimerInternal));
  assert(EC_SEMAPHORE_STRUCT_SIZE == sizeof(EcSemaphoreInternal));
  assert(EC_EVENT_STRUCT_SIZE == sizeof(EcEventInternal));
}


void EcOsStart(void) {
  EcTaskInternal* ti;

  /* Kick off threads */
  pthread_mutex_lock(&os_start_mutex);
  os_has_started = 1;
  pthread_cond_broadcast(&os_start_cond);
  pthread_mutex_unlock(&os_start_mutex);

  /* Wait for all task threads to run */
  while (1) {
    EcTaskInternal* ti_wait;

    /* Find the next task */
    pthread_mutex_lock(&task_list_mutex);
    ti_wait = task_list;
    pthread_mutex_unlock(&task_list_mutex);
    if (!ti_wait)
      break;  /* No tasks left */

    /* Wait for it to die */
    pthread_join(ti_wait->thread, NULL);

    /* Remove the dead thread from the list */
    pthread_mutex_lock(&task_list_mutex);
    if (task_list == ti_wait)
      task_list = ti_wait->next;
    else {
      for (ti = task_list; ti->next == ti_wait; ti = ti->next);
      if (ti)
        ti->next = ti_wait->next;
    }
    pthread_mutex_unlock(&task_list_mutex);
  }

  /* The remaining tasks for SWIs, etc, will die when the process exits */
}
