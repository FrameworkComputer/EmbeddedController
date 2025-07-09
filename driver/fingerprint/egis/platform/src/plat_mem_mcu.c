/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "plat_log.h"
#include "plat_mem.h"
#include "shared_mem.h"

void *sys_alloc(size_t count, size_t size)
{
	char *addr = NULL;
	int rc;

	rc = shared_mem_acquire(size, &addr);

	if (rc != EC_SUCCESS) {
		CPRINTS("Error - %s of size %u failed.", __func__, size);
		return NULL;
	}
	return addr;
}

void sys_free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}
	shared_mem_release(ptr);
}
