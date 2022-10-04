/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "panic.h"
#include "stack_trace.h"

void panic_assert_fail(const char *msg, const char *func, const char *fname,
		       int linenum)
{
	fprintf(stderr, "ASSERTION FAIL: %s:%d:%s - %s\n", fname, linenum, func,
		msg);
	task_dump_trace();

	puts("Fail!"); /* Inform test runner */
	fflush(stdout);

	exit(1);
}

void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
	struct panic_data *const pdata = panic_get_data();

	assert(pdata);
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH_X86;

	pdata->x86.vector = reason;
	pdata->x86.error_code = info;
	pdata->x86.eflags = exception;
}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	struct panic_data *const pdata = panic_get_data();

	assert(pdata);
	assert(pdata->struct_version == 2);

	if (reason)
		*reason = pdata->x86.vector;
	if (info)
		*info = pdata->x86.error_code;
	if (exception)
		*exception = pdata->x86.eflags;
}
