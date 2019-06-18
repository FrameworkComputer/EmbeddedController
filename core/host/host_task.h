/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator task scheduling module */

#ifndef __CROS_EC_HOST_TASK_H
#define __CROS_EC_HOST_TASK_H

#include <pthread.h>

#include "task.h"

/**
 * Returns the thread corresponding to the task.
 */
pthread_t task_get_thread(task_id_t tskid);

/**
 * Returns the ID of the active task, regardless of current thread
 * context.
 */
task_id_t task_get_running(void);

/**
 * Initializes the interrupt semaphore and associates a signal handler with
 * SIGNAL_INTERRUPT.
 */
void task_register_interrupt(void);

/**
 * Returns the process ID of the calling process.
 */
pid_t getpid(void);

#endif  /* __CROS_EC_HOST_TASK_H */
