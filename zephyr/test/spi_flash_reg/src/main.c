/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "spi_flash_reg.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(flash_reg_to_protect, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_reg_to_protect, test_invalid_args)
{
	unsigned int start = 0;
	unsigned int len = 0;

	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0, 0, NULL, &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0, 0, &start, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0xff, 0, &start, &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(0, 0xff, &start, &len));
}

ZTEST(flash_reg_to_protect, test_no_matching_range)
{
	unsigned int start = 0;
	unsigned int len = 0;

	/* Bad SR1 */
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP0, 0, &start,
					       &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP1, 0, &start,
					       &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_BP2, 0, &start,
					       &len));

	/* BAD SR2 */
	zassert_equal(EC_ERROR_INVAL, spi_flash_reg_to_protect(
					      SPI_FLASH_SR1_BP0,
					      SPI_FLASH_SR2_CMP, &start, &len));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_reg_to_protect(SPI_FLASH_SR1_SEC |
						       SPI_FLASH_SR1_BP0,
					       0, &start, &len));
}

ZTEST(flash_reg_to_protect, test_matching_range)
{
	unsigned int start = 0;
	unsigned int len = 0;

	zassert_equal(EC_SUCCESS, spi_flash_reg_to_protect(0, 0, &start, &len));
	zassert_equal(0, start);
	zassert_equal(0, len);
}

ZTEST_SUITE(flash_protect_to_reg, NULL, NULL, NULL, NULL, NULL);

ZTEST(flash_protect_to_reg, test_invalid_args)
{
	uint8_t sr1;
	uint8_t sr2;

	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0, 0, NULL, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(0, 0, &sr1, NULL));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(128, 0, &sr1, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(128, CONFIG_FLASH_SIZE_BYTES,
					       &sr1, &sr2));
	zassert_equal(EC_ERROR_INVAL,
		      spi_flash_protect_to_reg(128, 128, &sr1, &sr2));
}

ZTEST(flash_protect_to_reg, test_matching_range)
{
	uint8_t sr1;
	uint8_t sr2;

	zassert_equal(EC_SUCCESS,
		      spi_flash_protect_to_reg(0, 0x400000, &sr1, &sr2));
	zassert_equal(0x38, sr1, "Expected 0x38, but got 0x%02x", sr1);
	zassert_equal(0x00, sr2, "Expected 0x00, but got 0x%02x", sr2);

	zassert_equal(EC_SUCCESS,
		      spi_flash_protect_to_reg(0, 0x200000, &sr1, &sr2));
	zassert_equal(0x34, sr1, "Expected 0x38, but got 0x%02x", sr1);
	zassert_equal(0x00, sr2, "Expected 0x00, but got 0x%02x", sr2);
}
