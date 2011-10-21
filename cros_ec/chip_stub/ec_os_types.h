/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Operating system object types for EC.  These are
 * implementation-dependent; this file should come from the
 * implementation include directory. */

#ifndef __CROS_EC_OS_TYPES_H
#define __CROS_EC_OS_TYPES_H

#include "ec_common.h"

/* Structure sizes depend on the underlying implementation.  These
 * sizes are correct for the pthreads implementation. */
#define EC_TASK_STRUCT_SIZE 32
#define EC_SWI_STRUCT_SIZE 120
#define EC_TIMER_STRUCT_SIZE 120
#define EC_SEMAPHORE_STRUCT_SIZE 32
#define EC_EVENT_STRUCT_SIZE 104

/*****************************************************************************/
/* Tasks */

/* Task priority range */
#define EC_TASK_PRIORITY_LOWEST 0
#define EC_TASK_PRIORITY_DEFAULT 3
#define EC_TASK_PRIORITY_HIGHEST 7

/* Task instance.  Treat this as an opaque identifier. */
typedef struct EcTask {
  union {
    uint64_t align; /* Align on something big */
    uint8_t data[EC_TASK_STRUCT_SIZE];
  };
} EcTask;

/*****************************************************************************/
/* Software interrupts (SWI) */

/* SWI priority range */
#define EC_SWI_PRIORITY_LOWEST 0
#define EC_SWI_PRIORITY_DEFAULT 3
#define EC_SWI_PRIORITY_HIGHEST 7

/* SWI instance.  Treat this as an opaque identifier. */
typedef struct EcSwi {
  union {
    uint64_t align; /* Align on something big */
    uint8_t data[EC_SWI_STRUCT_SIZE];
  };
} EcSwi;

/*****************************************************************************/
/* Timers */

/* Timer priority range */
#define EC_TIMER_PRIORITY_LOWEST 0
#define EC_TIMER_PRIORITY_DEFAULT 3
#define EC_TIMER_PRIORITY_HIGHEST 7

/* Timer instance.  Treat this as an opaque identifier. */
typedef struct EcTimer {
  union {
    uint64_t align; /* Align on something big */
    uint8_t data[EC_TIMER_STRUCT_SIZE];
  };
} EcTimer;

/*****************************************************************************/
/* Semaphores */

/* Semaphore instance.  Treat this as an opaque identifier. */
typedef struct EcSemaphore {
  union {
    uint64_t align; /* Align on something big */
    uint8_t data[EC_SEMAPHORE_STRUCT_SIZE];
  };
} EcSemaphore;

/*****************************************************************************/
/* Events */

/* Event instance.  Treat this as an opaque identifier. */
typedef struct EcEvent {
  union {
    uint64_t align; /* Align on something big */
    uint8_t data[EC_EVENT_STRUCT_SIZE];
  };
} EcEvent;

#endif
