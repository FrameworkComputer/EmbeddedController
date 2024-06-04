/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "otp_key.h"
#include "test_util.h"
#include "util.h"

#include <stdbool.h>

void print_key_buffer(uint8_t *key_buff)
{
	ccprintf("key buffer: 0x");
	for (uint8_t i = 0; i < OTP_KEY_SIZE_BYTES; i++)
		ccprintf("%02X", key_buff[i]);
	ccprintf("\n");
}

test_static int test_otp_key(void)
{
	otp_key_init();

	uint32_t status = otp_key_provision();
	TEST_EQ(status, EC_SUCCESS, "%d");

	uint8_t otp_key_buffer[OTP_KEY_SIZE_BYTES] = { 0 };
	status = otp_key_read(otp_key_buffer);
	TEST_EQ(status, EC_SUCCESS, "%d");

	TEST_ASSERT(!bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES));

	print_key_buffer(otp_key_buffer);

	otp_key_exit();

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_otp_key);
	test_print_result();
}
