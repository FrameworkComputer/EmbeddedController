/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* One-Time Programmable (OTP) Key */

#include "chip/npcx/rom_chip.h"
#include "common.h"
#include "console.h"
#include "openssl/mem.h"
#include "otp_key.h"
#include "panic.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"

void otp_key_init(void)
{
	enum API_RETURN_STATUS_T status = API_RET_OTP_STATUS_FAIL;

	status = otpi_power(true);
	if (status != API_RET_OTP_STATUS_OK) {
		ccprintf("ERROR! %s failed %x\n", __func__, status);
		software_panic(PANIC_SW_ASSERT, task_get_current());
	}
}

void otp_key_exit(void)
{
	enum API_RETURN_STATUS_T status = API_RET_OTP_STATUS_FAIL;

	status = otpi_power(false);
	if (status != API_RET_OTP_STATUS_OK)
		ccprintf("ERROR! %s failed %x\n", __func__, status);
}

enum ec_error_list otp_key_read(uint8_t *key_buffer)
{
	enum API_RETURN_STATUS_T status = API_RET_OTP_STATUS_FAIL;
	uint8_t i;

	if (key_buffer == NULL)
		return EC_ERROR_INVAL;

	for (i = 0; i < OTP_KEY_SIZE_BYTES; i++) {
		status = otpi_read(OTP_KEY_ADDR + i, &key_buffer[i]);
		if (status != API_RET_OTP_STATUS_OK)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static enum ec_error_list otp_key_write(const uint8_t *key_buffer)
{
	enum API_RETURN_STATUS_T status = API_RET_OTP_STATUS_FAIL;
	uint8_t i;

	if (key_buffer == NULL)
		return EC_ERROR_INVAL;

	for (i = 0; i < OTP_KEY_SIZE_BYTES; i++) {
		status = otpi_write(OTP_KEY_ADDR + i, key_buffer[i]);
		if (status != API_RET_OTP_STATUS_OK)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

enum ec_error_list otp_key_provision(void)
{
	enum API_RETURN_STATUS_T otpi_status = API_RET_OTP_STATUS_FAIL;
	enum ec_error_list ec_status = EC_ERROR_UNKNOWN;
	uint8_t otp_key_buffer[OTP_KEY_SIZE_BYTES] = { 0 };

	ec_status = otp_key_read(otp_key_buffer);
	if (ec_status != EC_SUCCESS) {
		ccprints("Failed to read OTP key with status=%d", ec_status);
		return ec_status;
	}

	/*
	 * If the stored bytes are not trivial (all 0's or all 1's),
	 * key already written, return.
	 */
	if (!bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES))
		return EC_SUCCESS;

	/* Otherwise, generate and write key. */
	trng_init();
	trng_rand_bytes(otp_key_buffer, OTP_KEY_SIZE_BYTES);
	trng_exit();

	if (bytes_are_trivial(otp_key_buffer, OTP_KEY_SIZE_BYTES)) {
		ccprintf("ERROR! %s RNG failed!\n", __func__);
		software_panic(PANIC_SW_BAD_RNG, task_get_current());
	}

	ec_status = otp_key_write(otp_key_buffer);
	if (ec_status != EC_SUCCESS) {
		ccprints("failed to write OTP key, status=%d", ec_status);
		return EC_ERROR_UNKNOWN;
	}

	OPENSSL_cleanse(otp_key_buffer, OTP_KEY_SIZE_BYTES);

	otpi_status = otpi_write_protect(OTP_KEY_ADDR, OTP_KEY_SIZE_BYTES);
	if (otpi_status != API_RET_OTP_STATUS_OK) {
		ccprints("failed to write protect OTP key, status=%d",
			 otpi_status);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
