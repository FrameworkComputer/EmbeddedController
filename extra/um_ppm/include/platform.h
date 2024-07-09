/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_INCLUDE_PLATFORM_H_
#define UM_PPM_INCLUDE_PLATFORM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Platform independent utility functions.
 */

#define DLOG(fmt, ...)                                          \
	if (platform_debug_enabled()) {                         \
		platform_printf("DBG: %s: " fmt "\n", __func__, \
				##__VA_ARGS__);                 \
	}

#define DLOG_START(fmt, ...)                                               \
	if (platform_debug_enabled()) {                                    \
		platform_printf("DBG: %s: " fmt, __func__, ##__VA_ARGS__); \
	}

#define DLOG_LOOP(fmt, ...)                          \
	if (platform_debug_enabled()) {              \
		platform_printf(fmt, ##__VA_ARGS__); \
	}

#define DLOG_END(fmt, ...)                                \
	if (platform_debug_enabled()) {                   \
		platform_printf(fmt "\n", ##__VA_ARGS__); \
	}

#define ELOG(fmt, ...) \
	platform_eprintf("ERR: %s: " fmt "\n", __func__, ##__VA_ARGS__)

void *platform_malloc(size_t size);
void *platform_calloc(size_t nmemb, size_t size);
void platform_free(void *ptr);

void platform_memcpy(void *dest, const void *src, size_t length);
void platform_memset(void *dest, uint8_t data, size_t length);

void platform_set_debug(bool enable);
bool platform_debug_enabled();
void platform_printf(const char *format, ...);
void platform_eprintf(const char *format, ...);

void platform_usleep(uint32_t usec);

/* Opaque task id type.*/
struct task_handle;

/**
 * Initialize a task (code that can be independently scheduled).
 *
 * @param start_fn Start routine
 * @param arg Argument passed to start_fn
 * @param handle Task id type. If *handle is NULL, memory will be allocated.
 * @return 0 Success
 * @return -1 Fail
 */
int platform_task_init(void *start_fn, void *arg, struct task_handle **handle);

/* Called from within the task to complete / exit. */
void platform_task_exit();

/* Block on task completion (to clean up). */
int platform_task_complete(struct task_handle *handle);

/* Opaque mutex struct. */
struct platform_mutex;

/**
 * @brief Allocate and initialize a platform mutex.
 *
 * @param mutex POSIX mutex. If *mutex is NULL, memory will be allocated.
 * @retval 0 Success
 * @retval -1 Fail
 */
int platform_mutex_init(struct platform_mutex **mutex);

void platform_mutex_lock(struct platform_mutex *mutex);
void platform_mutex_unlock(struct platform_mutex *mutex);

/* Opaque notifier struct. */
struct platform_condvar;

/**
 * @brief Allocate and initialize a platform condvar.
 *
 * @param cond POSIX condvar. If *cond is NULL, memory will be allocated.
 * @retval 0 Success
 * @retval -1 Fail
 */
int platform_condvar_init(struct platform_condvar **cond);

void platform_condvar_wait(struct platform_condvar *condvar,
			   struct platform_mutex *mutex);
void platform_condvar_signal(struct platform_condvar *condvar);

#endif /* UM_PPM_INCLUDE_PLATFORM_H_ */
