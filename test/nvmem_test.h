/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_TEST_NVMEM_TEST_H
#define __EC_TEST_NVMEM_TEST_H

#define EMBEDDED_MODE 1
#define NV_C
#include "Global.h"
#undef NV_C
#include "NV_fp.h"
#include "tpm_generated.h"

enum test_failure_mode {
	TEST_NO_FAILURE,
	TEST_FAIL_WHEN_SAVING,
	TEST_FAIL_WHEN_INVALIDATING,
	TEST_FAIL_WHEN_COMPACTING,
	TEST_FAIL_SAVING_VAR,
	TEST_FAIL_FINALIZING_VAR,
	TEST_FAILED_HASH,
	TEST_SPANNING_PAGES
};

extern enum test_failure_mode failure_mode;

size_t add_evictable_obj(void *obj, size_t obj_size);
void drop_evictable_obj(void *obj);

#endif /* ! __EC_TEST_NVMEM_TEST_H */
