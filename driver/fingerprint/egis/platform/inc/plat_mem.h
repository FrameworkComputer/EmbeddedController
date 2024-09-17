/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_MEM_H_
#define __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_MEM_H_

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

#endif /* __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_MEM_H_ */
