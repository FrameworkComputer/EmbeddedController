/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Standard SPI master driver for Rotor MCU
 */

#include "common.h"
#include "dma.h"
#include "registers.h"
#include "spi.h"
#include "task.h"

static struct mutex spi_mutex[ROTOR_MCU_MAX_SSI_PORTS];

/* Default DMA channel options */
static struct dma_option dma_tx_option[] = {
	{
		.channel = ROTOR_MCU_DMAC_SPI0_TX,
		.periph = (void *)&ROTOR_MCU_SSI_DR(0, 0),
		.flags = 0, /* TODO(aaboagye): Create some flags. */
	},

	{
		.channel = ROTOR_MCU_DMAC_SPI1_TX,
		.periph = (void *)&ROTOR_MCU_SSI_DR(1, 0),
		.flags = 0, /* TODO(aaboagye): Create some flags. */
	},
};

static struct dma_option dma_rx_option[] = {
	{
		.channel = ROTOR_MCU_DMAC_SPI0_RX,
		.periph = (void *)&ROTOR_MCU_SSI_DR(0, 0),
		.flags = 0, /* TODO(aaboagye): Create some flags. */
	},

	{
		.channel = ROTOR_MCU_DMAC_SPI1_RX,
		.periph = (void *)&ROTOR_MCU_SSI_DR(1, 0),
		.flags = 0, /* TODO(aaboagye): Create some flags. */
	},
};

int spi_enable(int port, int enable)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int spi_transaction(const struct spi_device_t *spi_device,
		    const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int rv, port;

	port = spi_device->port;
	mutex_lock(spi_mutex + port);
	rv = spi_transaction_async(spi_device, txdata, txlen, rxdata, rxlen);
	mutex_unlock(spi_mutex + port);
	return EC_ERROR_UNIMPLEMENTED;
}

int spi_transaction_async(const struct spi_device_t *spi_device,
			  const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int spi_transaction_flush(const struct spi_device_t *spi_device)
{
	return EC_ERROR_UNIMPLEMENTED;
}

/**
 * Initialize a SPI port.
 *
 * @param port	SPI port to initialize.
 * @return EC_SUCCESS
 */
static int spi_master_initialize(int port)
{
	int i, div;

	/* Make sure port is valid. */
	ASSERT((port >= 0) && (port < ROTOR_MCU_MAX_SSI_PORTS));

	/* Disable the SSI module in order to make changes. */
	ROTOR_MCU_SSI_SSIENR(port) = 0;

	/* Find the greatest divisor for this port and set it. */
	div = 0;
	for (i = 0; i < spi_devices_used; i++)
		if ((spi_devices[i].port == port) &&
		    (div < spi_devices[i].div))
			div = spi_devices[i].div;
	ROTOR_MCU_SSI_BAUDR(port) = (div & 0xFFFF);

	/* Set 8-bit serial data transfer, SPI mode, and SPI_CLOCK_MODE0. */
	ROTOR_MCU_SSI_CTRLR0(port) = (0x7 << 16);

	return EC_SUCCESS;
}
