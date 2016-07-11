/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

struct cts_test {
	enum cts_rc (*run)(void);
	char *name;
};

#define CTS_TEST(test)	{test, STRINGIFY(test)},
struct cts_test tests[] = {
#include "cts.testlist"
};

#undef CTS_TEST
#define CTS_TEST(test)	CTS_TEST_ID_##test,
enum {
#include "cts.testlist"
	CTS_TEST_ID_COUNT,
};
