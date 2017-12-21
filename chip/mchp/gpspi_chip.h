/* Copyright 2017 The Chromium OS Authors. All rights reserved
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for MCHP MEC processor
 */
/** @file gpspi_chip.h
 *MCHP MEC General Purpose SPI Master
 */
/** @defgroup MCHP MEC gpspi
 */

#ifndef _GPSPI_CHIP_H
#define _GPSPI_CHIP_H

#include <stdint.h>
#include <stddef.h>

/* struct spi_device_t */
#include "spi.h"

int gpspi_transaction_flush(const struct spi_device_t *spi_device);

int gpspi_transaction_wait(const struct spi_device_t *spi_device);

int gpspi_transaction_async(const struct spi_device_t *spi_device,
				const uint8_t *txdata, int txlen,
				uint8_t *rxdata, int rxlen);

int gpspi_enable(int port, int enable);

#endif /* #ifndef _GPSPI_CHIP_H */
/**   @}
 */

