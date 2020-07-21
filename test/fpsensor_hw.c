/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "test_util.h"
#include "fpc_private.h"

/* Hardware-dependent smoke test that makes a SPI transaction with the
 * fingerprint sensor.
 */
test_static int test_fp_check_hwid(void)
{
	/* TODO(b/187134801): modify fpc_check_hwid() to return the hwid. */
	if (IS_ENABLED(SECTION_IS_RW))
		TEST_EQ(fpc_check_hwid(), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}


void run_test(int argc, char **argv)
{
	RUN_TEST(test_fp_check_hwid);
	test_print_result();
}
