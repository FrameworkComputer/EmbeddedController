/* Copyright 2017 The Chromium OS Authors. All rights reserved
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for MCHP MEC processor
 */
/** @file qmpis_chip.h
 *MCHP MEC Quad SPI Master
 */
/** @defgroup MCHP MEC qmspi
 */

#ifndef _SPI_CHIP_H
#define _SPI_CHIP_H

#include <stdint.h>
#include <stddef.h>

/* struct spi_device_t */
#include "spi.h"

#define SPI_DMA_OPTION_RD	0
#define SPI_DMA_OPTION_WR	1

/*
 * bits[3:0] = controller instance
 * bits[7:4] = controller family
 * 0 = QMSPI, 1 = GPSPI
 */
#define QMSPI0_PORT	0x00
#define GPSPI0_PORT	0x10
#define GPSPI1_PORT	0x11


#define QMSPI_CLASS0	0
#define GPSPI_CLASS0	1

#define QMSPI_CLASS	(0 << 4)
#define GPSPI_CLASS	BIT(4)

#define QMSPI_CTRL0	0
#define GPSPI_CTRL0	0
#define GPSPI_CTRL1	1

/*
 * Encode zero based controller class and instance values
 * in port value of spi_device_t.
 */
#define SPI_CTRL_ID(c, i) (((c & 0xf) << 4) + (i & 0xf))

/*
 * helper to return pointer to QMSPI or GPSPI struct dma_option
 */
const void *spi_dma_option(const struct spi_device_t *spi_device,
				int is_tx);

#endif /* #ifndef _QMSPI_CHIP_H */
/**   @}
 */

