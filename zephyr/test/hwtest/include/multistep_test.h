/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _HWTEST_MULTISTEP_TEST_
#define _HWTEST_MULTISTEP_TEST_

#include "system.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_test.h>

/* Unique step zero for tests. Divide by 2 not to overflow scratchpad area and
 * shift to assume 0 is an incorrect value for step zero.
 */
#define TEST_STEP_ZERO(steps) \
	((crc16_itu_t(0, (const uint8_t *)(steps), sizeof(steps)) / 2) + 1)

/**
 * @brief Register a multi-step test.
 *
 * Next step will be called after reboot/crash.
 *
 * @param name Name of a test.
 * @param steps Array of steps which cause reboot/crash. Each step is a pointer
 * to a void function that takes no arguments.
 */
#define MULTISTEP_TEST(name, steps)                                          \
	static void *multisetep_test_setup(void)                             \
	{                                                                    \
		uint32_t step = 0;                                           \
		int ret = 0;                                                 \
                                                                             \
		ret = system_get_scratchpad(&step);                          \
		if ((step < TEST_STEP_ZERO(steps)) ||                        \
		    (step >= (TEST_STEP_ZERO(steps) + ARRAY_SIZE(steps)))) { \
			system_set_scratchpad(TEST_STEP_ZERO(steps));        \
		}                                                            \
		zassert_equal(ret, 0);                                       \
                                                                             \
		return NULL;                                                 \
	}                                                                    \
	static void multisetep_test_teardown(void *fixture)                  \
	{                                                                    \
		system_set_scratchpad(0);                                    \
	}                                                                    \
	ZTEST_SUITE(name, NULL, multisetep_test_setup, NULL, NULL,           \
		    multisetep_test_teardown);                               \
	ZTEST(name, test_##name)                                             \
	{                                                                    \
		uint32_t step = 0;                                           \
		int ret = 0;                                                 \
                                                                             \
		ret = system_get_scratchpad(&step);                          \
		zassert_equal(ret, 0);                                       \
		ret = system_set_scratchpad(step + 1);                       \
                                                                             \
		steps[step - TEST_STEP_ZERO(steps)]();                       \
	}                                                                    \
                                                                             \
	/* If the test shell is enabled, the test will be run once by a test \
	 * runner. The steps call cause reboot/crash, which means we need to \
	 * run it again.                                                     \
	 */                                                                  \
	IF_ENABLED(CONFIG_ZTEST_SHELL,                                       \
		   (                                                         \
			   static struct k_work multistep_test_work;         \
			   static void multistep_test_handler(               \
				   struct k_work *work) {                    \
				   uint32_t step = 0;                        \
                                                                             \
				   system_get_scratchpad(&step);             \
				   if ((step > TEST_STEP_ZERO(steps)) &&     \
				       (step < (TEST_STEP_ZERO(steps) +      \
						ARRAY_SIZE(steps)))) {       \
					   ztest_run_test_suite(name, false, \
								1, 1);       \
				   }                                         \
			   }                                                 \
                                                                             \
			   static int multistep_test_init(void) {            \
				   k_work_init(&multistep_test_work,         \
					       multistep_test_handler);      \
                                                                             \
				   /* Check if the test has to be run after  \
				    * reboot */                              \
				   k_work_submit(&multistep_test_work);      \
                                                                             \
				   return 0;                                 \
			   } SYS_INIT(multistep_test_init, POST_KERNEL,      \
				      CONFIG_APPLICATION_INIT_PRIORITY);))

#endif /* _HWTEST_MULTISTEP_TEST_ */
