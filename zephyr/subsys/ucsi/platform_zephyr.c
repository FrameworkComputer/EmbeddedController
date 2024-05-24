/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/platform.h"
#include "ppm_common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread_stack.h>

bool debug_enabled;

void *platform_calloc(size_t nmemb, size_t size)
{
	return NULL;
}

void platform_free(void *ptr)
{
}

void platform_memcpy(void *dest, const void *src, size_t length)
{
	memcpy(dest, src, length);
}

void platform_memset(void *dest, uint8_t data, size_t length)
{
	memset(dest, data, length);
}

void platform_set_debug(bool enable)
{
	debug_enabled = enable;
}

bool platform_debug_enabled(void)
{
	return debug_enabled;
}

void platform_printf(const char *format, ...)
{
	va_list arglist;

	va_start(arglist, format);
	vprintf(format, arglist);
	va_end(arglist);
}

void platform_eprintf(const char *format, ...)
{
	va_list arglist;

	va_start(arglist, format);
	vfprintf(stderr, format, arglist);
	va_end(arglist);
}

void platform_usleep(uint32_t usec)
{
	k_usleep(usec);
}

struct task_handle {
	k_tid_t thread;
	struct k_thread thread_data;
};

#define STACK_SIZE (1024)
K_THREAD_STACK_DEFINE(stack, STACK_SIZE);

int platform_task_init(void *start_fn, void *arg, struct task_handle **handle)
{
	if (!*handle) {
		ELOG("Handle=NULL not supported");
		return -1;
	}

	(*handle)->thread = k_thread_create(
		&(*handle)->thread_data, stack, STACK_SIZE, start_fn, arg, 0, 0,
		CONFIG_PDC_POWER_MGMT_THREAD_PRIORTY, 0, K_NO_WAIT);

	return 0;
}

void platform_task_exit(void)
{
	k_thread_abort(k_current_get());
}

int platform_task_complete(struct task_handle *handle)
{
	return k_thread_join(&handle->thread_data, K_FOREVER);
}

struct platform_mutex {
	struct k_mutex lock;
};

int platform_mutex_init(struct platform_mutex **mutex)
{
	if (!*mutex) {
		return -1;
	}

	if (k_mutex_init(&(*mutex)->lock)) {
		return -1;
	}

	return 0;
}

void platform_mutex_lock(struct platform_mutex *mutex)
{
	k_mutex_lock(&mutex->lock, K_FOREVER);
}

void platform_mutex_unlock(struct platform_mutex *mutex)
{
	k_mutex_unlock(&mutex->lock);
}

struct platform_condvar {
	struct k_condvar var;
};

int platform_condvar_init(struct platform_condvar **cond)
{
	if (!*cond) {
		return -1;
	}

	if (k_condvar_init(&(*cond)->var)) {
		return -1;
	}

	return 0;
}

void platform_condvar_wait(struct platform_condvar *condvar,
			   struct platform_mutex *mutex)
{
	k_condvar_wait(&condvar->var, &mutex->lock, K_FOREVER);
}

void platform_condvar_signal(struct platform_condvar *condvar)
{
	k_condvar_signal(&condvar->var);
}

struct ucsi_ppm_driver *platform_allocate_ppm(void)
{
	/* These are all set to 0 by default. */
	static struct ppm_common_device dev;
	static struct ucsi_ppm_driver drv;
	static struct platform_condvar ppm_condvar;
	static struct platform_mutex ppm_lock;
	static struct task_handle ppm_task_handle;

	drv.dev = (struct ucsi_ppm_device *)&dev;
	dev.ppm_condvar = &ppm_condvar;
	dev.ppm_lock = &ppm_lock;
	dev.ppm_task_handle = &ppm_task_handle;

	return &drv;
}
