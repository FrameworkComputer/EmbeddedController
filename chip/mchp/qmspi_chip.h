/* Copyright 2017 The Chromium OS Authors. All rights reserved
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for MEC17xx processor
 */
/** @file qmspi_chip.h
 *MEC17xx Quad SPI Master
 */
/** @defgroup MCHP MEC qmspi
 */

#ifndef _QMSPI_CHIP_H
#define _QMSPI_CHIP_H

#include <stdint.h>
#include <stddef.h>

/* struct spi_device_t */
#include "spi.h"


int qmspi_transaction_flush(const struct spi_device_t *spi_device);

int qmspi_transaction_wait(const struct spi_device_t *spi_device);

int qmspi_transaction_sync(const struct spi_device_t *spi_device,
				const uint8_t *txdata, int txlen,
				uint8_t *rxdata, int rxlen);

int qmspi_transaction_async(const struct spi_device_t *spi_device,
				const uint8_t *txdata, int txlen,
				uint8_t *rxdata, int rxlen);

int qmspi_enable(int port, int enable);

/*
 * QMSPI0 Start
 * flags
 *  b[0] = ignored
 *  b[1] = 1 enable QMSPI interrupts
 *  b[2] = 1 start
 */
void qmspi_cfg_irq_start(uint8_t flags);

/*
 * QMSPI transmit and/or receive
 * np_flags
 *  b[7:0] = flags
 *	b[0] = close(de-assert chip select when done)
 *	b[1] = enable Done and ProgError interrupt
 *	b[2] = start
 *  b[15:8] = number of tx pins
 *  b[24:16] = number of rx pins
 *
 * returns last descriptor 0 <= index < MCHP_QMSPI_MAX_DESCR
 * or error (bit[7]==1)
 */
uint8_t qmspi_xfr(const struct spi_device_t *spi_device,
			uint32_t np_flags,
			const uint8_t *txdata, uint32_t ntx,
			uint8_t *rxdata, uint32_t nrx);

#endif /* #ifndef _QMSPI_CHIP_H */
/**   @}
 */

