/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "util.h"

/*
 * CTS_TEST macro takes the following arguments:
 *
 * @test: Function running the test
 * @th_rc: Expected CTS_RC_* from TH
 * @th_string: Expected string printed by TH
 * @dut_rc: Expected CTR_RC_* from DUT
 * @dut_string: Expected string printed by DUT
 *
 * CTS_TEST macro is processed in multiple places. One is here for creating
 * an array of test functions. Only @test is used.
 *
 * Another is in cts.py for evaluating the test results against expectations.
 */

#undef CTS_TEST
#define CTS_TEST(test, th_rc, th_string, dut_rc, dut_string) \
	{test, STRINGIFY(test)},
struct cts_test tests[] = {
#include "cts.testlist"
};

const int cts_test_count = ARRAY_SIZE(tests);
