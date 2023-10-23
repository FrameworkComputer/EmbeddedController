/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "shared_mem.h"

#include <zephyr/ztest_test.h>

ZTEST_SUITE(shared_mem, NULL, NULL, NULL, NULL, NULL);

ZTEST(shared_mem, test_release_null)
{
	/* This should be a no-op. We can't do much to test it directly. */
	shared_mem_release(NULL);
}
