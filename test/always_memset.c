/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * We enable optimization level 3 in the test/build.mk.
 * Running these tests without optimization is pointless, since the primary
 * purpose of the always_memset function is to evade compiler optimizations.
 * If optimization is disabled, the test_optimization_working test will fail.
 */

#include <string.h>

#include "common.h"
#include "test_util.h"

#include "cryptoc/util.h"

/* 256 bytes of stack is only safe enough for a memcpy. */
#define EXTRA_STACK_SIZE 256
#define UNIQUE_STRING "Hello World!"

/**
 * @brief Check basic memset behavior of always_memset.
 */
test_static int test_basic_functionality(void)
{
	char buf[256];

	for (size_t i = 0; i < sizeof(buf); i++) {
		buf[i] = (char)i;
	}

	always_memset(buf, 1, sizeof(buf));

	TEST_ASSERT_MEMSET(buf, 1, sizeof(buf));

	return EC_SUCCESS;
}

/**
 * @brief Builtin memset stand-in.
 *
 * The compiler doesn't see our EC memset as a function that can be optimized
 * out "with no side effect", so we present one here.
 */
test_static inline __attribute__((always_inline)) void
fake_builtin_memset(char *dest, char c, size_t len)
{
	for (size_t i = 0; i < len; i++)
		dest[i] = c;
}

/**
 * This function creates a contrived scenario where the compiler would choose
 * to optimize out the last memset due to no side effect.
 *
 * This methodology/setup has also been manually tested using the real builtin
 * memset with -O3 in a GNU/Linux environment using GCC-12.2.0 and clang-14.0.6.
 *
 * Notes for manually verifying:
 * - Run outright with normal memset. Ensure that you can still read the
 *   UNIQUE_STRING from p.
 * - Use GDB.
 * - Set break point at |exercise_memset| and run.
 * - Run "info locals" to check the state of |space| and |buf|.
 * - Run "p &space" and "p &buf". Ensure both are real stack addresses and
 *   that buf's address is lower than space's address.
 *   Ensure the size of |space|.
 * - Save the address of |space| for later by doing "set $addr = &space".
 * - Set a mem watch on |buf| by running "watch buf".
 * - Run "c" to continue the function. Ensure that the only part of the function
 *   that touches this variable is the initialization.
 * - If there seems to be something odd happening, open "layout split" and
 *   step instruction-by-instruction using "si".
 *   Note that memset will not be a function call and will not always
 *   look the same. It will probably be tightly integrated with other
 *   functionality.
 * - To check how much of the stack |space| was used by the caller,
 *   run "x/256xb $adr" or "x/64xw $adr" after the memcpy.
 */
test_static void exercise_memset(char **p)
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

	/* Expect the following memset to be omitted during optimization. */
	fake_builtin_memset(buf, 0, sizeof(buf));
}

/**
 * Ensure that optimization is removing a trailing memset that it deems to have
 * no side-effect.
 */
test_static int test_optimization_working(void)
{
	char buf[sizeof(UNIQUE_STRING)];
	char *p;

	exercise_memset(&p);
	memcpy(buf, p, sizeof(buf));

	/*
	 * We expect that the compiler would have optimized out the final
	 * memset, thus we should still be able to see the UNIQUE_STRING in
	 * memory.
	 */
	TEST_ASSERT_ARRAY_EQ(buf, UNIQUE_STRING, sizeof(buf));

	return EC_SUCCESS;
}

/**
 * This function creates a contrived scenario where the compiler would choose
 * to optimize out the last memset due to no side effect.
 *
 * This function layout must remain identical to exercise_memset.
 */
test_static void exercise_always_memset(char **p)
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

	/* Expect the following memset to NOT be omitted during optimization. */
	always_memset(buf, 0, sizeof(buf));
}

/**
 * Ensure that always_memset works when used in a scenario where a normal
 * memset would be removed.
 */
test_static int test_always_memset(void)
{
	char buf[sizeof(UNIQUE_STRING)];
	char *p;

	exercise_always_memset(&p);
	memcpy(buf, p, sizeof(buf));

	TEST_ASSERT_MEMSET(buf, 0, sizeof(buf));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_basic_functionality);
	RUN_TEST(test_optimization_working);
	RUN_TEST(test_always_memset);

	test_print_result();
}
