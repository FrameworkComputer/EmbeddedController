/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests RSA implementation.
 */

#include "console.h"
#include "common.h"
#include "rsa.h"
#include "test_util.h"
#include "util.h"

#ifdef TEST_RSA3
#include "rsa2048-3.h"
#else
#include "rsa2048-F4.h"
#endif

static uint32_t rsa_workbuf[3 * RSANUMBYTES/4];

void run_test(int argc, char **argv)
{
	int good;

	good = rsa_verify(rsa_key, sig, hash, rsa_workbuf);
	if (!good) {
		ccprintf("RSA verify FAILED\n");
		test_fail();
		return;
	}
	ccprintf("RSA verify OK\n");

	/* Test with a wrong hash */
	good = rsa_verify(rsa_key, sig, hash_wrong, rsa_workbuf);
	if (good) {
		ccprintf("RSA verify OK (expected fail)\n");
		test_fail();
		return;
	}
	ccprintf("RSA verify FAILED (as expected)\n");

	/* Test with a wrong signature */
	good = rsa_verify(rsa_key, sig+1, hash, rsa_workbuf);
	if (good) {
		ccprintf("RSA verify OK (expected fail)\n");
		test_fail();
		return;
	}
	ccprintf("RSA verify FAILED (as expected)\n");

	test_pass();
}

