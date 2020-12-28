/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Various utility for unit testing */

#ifndef __CROS_EC_TEST_UTIL_H
#define __CROS_EC_TEST_UTIL_H

#include <ztest.h>
#include "ec_tasks.h"

/*
 * We need these macros so that a test can be built for either Ztest or the
 * EC test framework.
 *
 * Ztest unit tests are void and do not return a value. In the EC framework,
 * if none of the assertions fail, the test is supposed to return EC_SUCCESS,
 * so just define that as empty and `return EC_SUCCESS;` will get pre-processed
 * into `return ;`
 */
#define EC_TEST_RETURN void
#define EC_SUCCESS
#define test_pass ztest_test_pass

/* Zephyr threads have three void pointers as parameters */
#define TASK_PARAMS void *p1, void *p2, void *p3

uint32_t prng(uint32_t seed);

uint32_t prng_no_seed(void);

#endif /* __CROS_EC_TEST_UTIL_H */
