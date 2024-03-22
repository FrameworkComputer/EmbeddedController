/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Mock OTPI library
 */
#include "mock/otpi_mock.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_otp mock_otp = MOCK_OTP_DEFAULT;

/* true: OTP hardware on, false: OTP hardware off */
enum API_RETURN_STATUS_T otpi_power(bool on)
{
	mock_otp.powered_on = on;
	return API_RET_OTP_STATUS_OK;
}

/*
 * address - OTP address to read from
 * data - pointer to 8-bit variable to store read data
 */
enum API_RETURN_STATUS_T otpi_read(uint32_t address, uint8_t *data)
{
	if (!mock_otp.powered_on)
		return API_RET_OTP_STATUS_FAIL;

	*data = mock_otp.otp_key_buffer[address - OTP_KEY_ADDR];
	return API_RET_OTP_STATUS_OK;
}

/*
 * address - OTP address to write to
 * data -  8-bit data value
 */
enum API_RETURN_STATUS_T otpi_write(uint32_t address, uint8_t data)
{
	if (!mock_otp.powered_on)
		return API_RET_OTP_STATUS_FAIL;

	mock_otp.otp_key_buffer[address - OTP_KEY_ADDR] |= data;
	return API_RET_OTP_STATUS_OK;
}

/*
 * address - OTP address to protect, 16B aligned
 * size - Number of bytes to be protected, 16B aligned
 */
enum API_RETURN_STATUS_T otpi_write_protect(uint32_t address, uint32_t size)
{
	if (!mock_otp.powered_on)
		return API_RET_OTP_STATUS_FAIL;

	return API_RET_OTP_STATUS_OK;
}
