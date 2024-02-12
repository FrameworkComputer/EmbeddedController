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
	uint8_t i;

	ccprintf("key buffer: 0x");
	for (i = 0; i < OTP_KEY_SIZE_BYTES; i++)
		ccprintf("%02X", key_buff[i]);
	ccprintf("\n");
}

test_static int test_otp_key(void)
{
	uint32_t status;
	uint8_t otp_key_buffer[OTP_KEY_SIZE_BYTES] = { 0 };

	otp_key_init();

	ccprints("OTP Key provision");
	status = otp_key_provision();
	if (status != EC_SUCCESS) {
		ccprints("Failed to read OTP key");
		return EC_ERROR_UNKNOWN;
	}

	ccprints("OTP Key read");
	status = otp_key_read(otp_key_buffer);
	if (status != EC_SUCCESS) {
		ccprints("Failed to read OTP key");
		return EC_ERROR_UNKNOWN;
	}

	if (bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES)) {
		ccprints("Key is trivial after provisioning, fail test");
		return EC_ERROR_UNCHANGED;
	}

	print_key_buffer(otp_key_buffer);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	ccprintf("Running otp_key test\n");
	RUN_TEST(test_otp_key);
	test_print_result();
}
