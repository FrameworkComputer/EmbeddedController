/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <rollback.h>
#include <rollback_private.h>

/* Verify that secure_clear function survives under -O2/-Os, but memset doesn't.
 */

/* 256 bytes of stack is only safe enough for a memcpy. */
#define EXTRA_STACK_SIZE 256
#define UNIQUE_STRING "Hello World!"

static void run_memset(char **p, bool use_secure_clear)
{
	/*
	 * Add extra stack space so that |buf| doesn't get trampled while the
	 * caller is processing the |p| we set (ex. printing and testing p).
	 *
	 * Without volatile, space will be optimized out.
	 */
	volatile char __unused space[EXTRA_STACK_SIZE] = {
		[0 ... EXTRA_STACK_SIZE - 1] = 's'
	};

	char buf[] = UNIQUE_STRING;
	*p = buf;

	/*
	 * Force access to |buf| to ensure that it is allocated and seen as
	 * used. Without casting to volatile, the access would be optimized out.
	 * We don't want to make |buf| itself volatile, since
	 * we want the compiler to optimize out the final memset.
	 */
	for (size_t i = 0; i < sizeof(buf); i++)
		(void)((volatile char *)buf)[i];

	if (use_secure_clear) {
		secure_clear(buf, sizeof(buf));
	} else {
		memset(buf, 0, sizeof(buf));
	}
}

ZTEST(secure_clear, test_secure_clear)
{
	char *p;

	run_memset(&p, true);
	/* Verify that secure_clear wipes the memory */
	for (int i = 0; i < strlen(UNIQUE_STRING); i++) {
		zassert_equal(p[i], 0);
	}
}

#ifndef CONFIG_NO_OPTIMIZATIONS
ZTEST(secure_clear, test_memset)
{
	char *p;

	run_memset(&p, false);
	/* Verify that memset is not called */
	zassert_mem_equal(p, UNIQUE_STRING, strlen(UNIQUE_STRING));
}
#endif

ZTEST_SUITE(secure_clear, NULL, NULL, NULL, NULL, NULL);
