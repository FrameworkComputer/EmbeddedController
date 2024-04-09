/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "link_defs.h"
#include "shared_mem.h"

#include <errno.h>

#include <zephyr/ztest.h>

ZTEST_SUITE(sbrk, NULL, NULL, NULL, NULL, NULL);

#ifdef CONFIG_FAKE_SHMEM
uintptr_t system_usable_ram_end(void)
{
	return (uintptr_t)__shared_mem_buf + shared_mem_size();
}
#endif /* CONFIG_FAKE_SHMEM */

extern void *sbrk(intptr_t count);

static uint8_t *start;

/* Store the start of heap before a test run, because ztest uses malloc */
static int sbrk_init(void)
{
	start = sbrk(0);

	return 0;
}
SYS_INIT(sbrk_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ZTEST(sbrk, test_sbrk_start)
{
	zassert_equal(start, (uint8_t *)__shared_mem_buf);
}

ZTEST(sbrk, test_sbrk)
{
	uint8_t *prev;
	uint8_t *start_test;

	start_test = sbrk(0);

	prev = sbrk(100);
	/* ztest shell uses malloc, so the following check is incorrect
	 * zassert_equal(prev, start);
	 */
	zassert_equal(start_test, prev);

	uint8_t *cur = sbrk(0);
	zassert_equal(cur, prev + 100);

	prev = sbrk(-100);
	zassert_equal(prev, cur);

	cur = sbrk(0);
	zassert_equal(cur, start_test);
}

ZTEST(sbrk, test_sbrk_overflow)
{
	/* Requesting the maximum possible amount should succeed. */
	intptr_t heap_free = system_usable_ram_end() - (uintptr_t)sbrk(0);
	uint8_t *ptr = sbrk(heap_free);
	zassert_not_equal(ptr, (void *)-1);

	/* Requesting any more should fail. */
	ptr = sbrk(1);

	zassert_equal(ptr, (void *)-1);
	zassert_equal(errno, ENOMEM);

	sbrk(-heap_free);
}

ZTEST(sbrk, test_sbrk_underflow)
{
	intptr_t heap_start = (uintptr_t)sbrk(0);
	intptr_t heap_used = heap_start - (uintptr_t)__shared_mem_buf;

	/* Return one more byte than already used. */
	uint8_t *ptr = sbrk(-(heap_used + 1));
	zassert_equal(ptr, (void *)-1);
	zassert_equal(errno, ENOMEM);

	ptr = sbrk(0);
	zassert_equal((uintptr_t)ptr, heap_start);
}
