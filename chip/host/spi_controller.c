/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock SPI Controller driver for unit test.
 */

#include <stdint.h>

#include "common.h"
#include "gpio.h"

#include "spi.h"

test_mockable int spi_enable(const struct spi_device_t *spi_device, int enable)
{
	return EC_SUCCESS;
}

test_mockable int spi_transaction(const struct spi_device_t *spi_device,
				  const uint8_t *txdata, int txlen,
				  uint8_t *rxdata, int rxlen)
{
	return EC_SUCCESS;
}

test_mockable int spi_transaction_async(const struct spi_device_t *spi_device,
					const uint8_t *txdata, int txlen,
					uint8_t *rxdata, int rxlen)
{
	return EC_SUCCESS;
}

test_mockable int spi_transaction_flush(const struct spi_device_t *spi_device)
{
	return EC_SUCCESS;
}

test_mockable int spi_transaction_wait(const struct spi_device_t *spi_device)
{
	return EC_SUCCESS;
}
