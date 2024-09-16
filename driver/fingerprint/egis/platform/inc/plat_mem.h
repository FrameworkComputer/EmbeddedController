/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __PLAT_MEM_H__
#define __PLAT_MEM_H__

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void *sys_alloc(size_t count, size_t size);
void sys_free(void *data);
#define plat_alloc(fmt) sys_alloc(1, fmt)
#define plat_free(x) sys_free(x)
#define PLAT_FREE(x)          \
	if (x != NULL) {      \
		plat_free(x); \
		x = NULL;     \
	}
#endif