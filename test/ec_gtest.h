/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Tests that want to utilize googletest should include this header file.
 *
 * There's no need to include the googletest header files directly.
 */

#ifndef __CROS_EC_TEST_EC_GTEST_H
#define __CROS_EC_TEST_EC_GTEST_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

static inline void run_all_googletest_tests()
{
	testing::InitGoogleTest();

	int ret = RUN_ALL_TESTS();

	if (ret == 0)
		printf("Pass!\n");
	else
		printf("Fail!\n");
}

extern "C" void run_test(int, const char **)
{
	run_all_googletest_tests();
}

#endif /* __CROS_EC_TEST_EC_GTEST_H */
