/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "plat_log.h"
#include "spi.h"

#define LOG_TAG "PLAT-SPI"

int periphery_spi_write_read(uint8_t *tx_addr, uint32_t tx_len, uint8_t *rx_buf,
			     uint32_t rx_len)
{
	int retval = 0;

	retval = spi_transaction(&spi_devices[0], tx_addr, tx_len, rx_buf,
				 rx_len);
	if (retval != 0) {
		egislog_e("spi_write FAILED: retval = %d\n", __func__, retval);
	}
	return retval;
}
