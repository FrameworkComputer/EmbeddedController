/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * CTS_TEST macro is used by dut.c, th.c, and cts.py. Currently, the 2nd
 * and 3rd arguments are only used by cts.py. They specify the expected
 * strings output by TH and DUT, respectively.
 */

struct cts_test {
	enum cts_rc (*run)(void);
	char *name;
};

#define CTS_TEST(test, th_rc, th_string, dut_rc, dut_string) \
	{test, STRINGIFY(test)},
struct cts_test tests[] = {
#include "cts.testlist"
};

#undef CTS_TEST
#define CTS_TEST(test, th_rc, th_string, dut_rc, dut_string) CTS_TEST_ID_##test,
enum {
#include "cts.testlist"
	CTS_TEST_ID_COUNT,
};
