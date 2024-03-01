/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

bool debug_enabled = false;

void *platform_malloc(size_t size)
{
	return malloc(size);
}
void *platform_calloc(size_t nmemb, size_t size)
{
	return calloc(nmemb, size);
}
void platform_free(void *ptr)
{
	free(ptr);
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
bool platform_debug_enabled()
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
	usleep(usec);
}

void platform_hexdump(const uint8_t *data, size_t len)
{
	for (int _i = 0; _i < len; ++_i) {
		DLOG_LOOP("0x%02x", data[_i]);
		if (_i != len - 1)
			DLOG_LOOP(", ");
	}
}

struct task_handle {
	pthread_t thread;
};

struct task_handle *platform_task_init(void *start_fn, void *arg)
{
	struct task_handle *handle = malloc(sizeof(struct task_handle));
	if (!handle) {
		ELOG("Failed to allocate task handle");
		return NULL;
	}

	int res = pthread_create(&handle->thread, NULL, start_fn, arg);
	if (res != 0) {
		ELOG("Failed to start thread with error %d for start_fn %p",
		     res, start_fn);
		free(handle);
		return NULL;
	}

	return handle;
}

void platform_task_exit()
{
	pthread_exit(NULL);
}

int platform_task_complete(struct task_handle *handle)
{
	return pthread_join(handle->thread, NULL);
}

struct platform_mutex {
	pthread_mutex_t lock;
};

struct platform_mutex *platform_mutex_init()
{
	struct platform_mutex *mutex = malloc(sizeof(struct platform_mutex));
	if (!mutex) {
		return NULL;
	}

	if (pthread_mutex_init(&mutex->lock, NULL)) {
		free(mutex);
		return NULL;
	}

	return mutex;
}

void platform_mutex_lock(struct platform_mutex *mutex)
{
	pthread_mutex_lock(&mutex->lock);
}
void platform_mutex_unlock(struct platform_mutex *mutex)
{
	pthread_mutex_unlock(&mutex->lock);
}

struct platform_condvar {
	pthread_cond_t var;
};

struct platform_condvar *platform_condvar_init()
{
	struct platform_condvar *cond = malloc(sizeof(struct platform_condvar));
	if (!cond) {
		return NULL;
	}

	if (pthread_cond_init(&cond->var, NULL)) {
		free(cond);
		return NULL;
	}

	return cond;
}

void platform_condvar_wait(struct platform_condvar *condvar,
			   struct platform_mutex *mutex)
{
	pthread_cond_wait(&condvar->var, &mutex->lock);
}

void platform_condvar_signal(struct platform_condvar *condvar)
{
	pthread_cond_signal(&condvar->var);
}
