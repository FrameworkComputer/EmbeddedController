/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SPI master module for MEC1322 */

#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "timer.h"
#include "util.h"
#include "hooks.h"
#include "task.h"

#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)

#define SPI_BYTE_TRANSFER_TIMEOUT_US (3 * MSEC)
#define SPI_BYTE_TRANSFER_POLL_INTERVAL_US 100

#define SPI_DMA_CHANNEL(port) (MEC1322_DMAC_SPI0_RX + (port) * 2)

/* only regular image needs mutex, LFW does not have scheduling */
/* TODO: Move SPI locking to common code */
#ifndef LFW
static struct mutex spi_mutex;
#endif

static const struct dma_option spi_rx_option[] = {
	{
		SPI_DMA_CHANNEL(0),
		(void *)&MEC1322_SPI_RD(0),
		MEC1322_DMA_XFER_SIZE(1)
	},
	{
		SPI_DMA_CHANNEL(1),
		(void *)&MEC1322_SPI_RD(1),
		MEC1322_DMA_XFER_SIZE(1)
	},
};

static int wait_byte(const int port)
{
	timestamp_t deadline;

	deadline.val = get_time().val + SPI_BYTE_TRANSFER_TIMEOUT_US;
	while ((MEC1322_SPI_SR(port) & 0x3) != 0x3) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		usleep(SPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}
	return EC_SUCCESS;
}

static int spi_tx(const int port, const uint8_t *txdata, int txlen)
{
	int i;
	int ret = EC_SUCCESS;
	uint8_t unused __attribute__((unused)) = 0;

	for (i = 0; i < txlen; ++i) {
		MEC1322_SPI_TD(port) = txdata[i];
		ret = wait_byte(port);
		if (ret != EC_SUCCESS)
			return ret;
		unused = MEC1322_SPI_RD(port);
	}

	return ret;
}

int spi_transaction_async(const struct spi_device_t *spi_device,
			  const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	int port = spi_device->port;
	int ret = EC_SUCCESS;

	gpio_set_level(spi_device->gpio_cs, 0);

	/* Disable auto read */
	MEC1322_SPI_CR(port) &= ~BIT(5);

	ret = spi_tx(port, txdata, txlen);
	if (ret != EC_SUCCESS)
		return ret;

	/* Enable auto read */
	MEC1322_SPI_CR(port) |= BIT(5);

	if (rxlen != 0) {
		dma_start_rx(&spi_rx_option[port], rxlen, rxdata);
		MEC1322_SPI_TD(port) = 0;
	}
	return ret;
}

int spi_transaction_flush(const struct spi_device_t *spi_device)
{
	int port = spi_device->port;
	int ret = dma_wait(SPI_DMA_CHANNEL(port));
	uint8_t unused __attribute__((unused)) = 0;

	timestamp_t deadline;

	/* Disable auto read */
	MEC1322_SPI_CR(port) &= ~BIT(5);

	deadline.val = get_time().val + SPI_BYTE_TRANSFER_TIMEOUT_US;
	/* Wait for FIFO empty SPISR_TXBE */
	while ((MEC1322_SPI_SR(port) & 0x01) != 0x1) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		usleep(SPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}

	dma_disable(SPI_DMA_CHANNEL(port));
	dma_clear_isr(SPI_DMA_CHANNEL(port));
	if (MEC1322_SPI_SR(port) & 0x2)
		unused = MEC1322_SPI_RD(port);

	gpio_set_level(spi_device->gpio_cs, 1);

	return ret;
}

int spi_transaction(const struct spi_device_t *spi_device,
		    const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int ret;

#ifndef LFW
	mutex_lock(&spi_mutex);
#endif
	ret = spi_transaction_async(spi_device, txdata, txlen, rxdata, rxlen);
	if (ret)
		return ret;
	ret = spi_transaction_flush(spi_device);

#ifndef LFW
	mutex_unlock(&spi_mutex);
#endif
	return ret;
}

int spi_enable(int port, int enable)
{
	if (enable) {
		gpio_config_module(MODULE_SPI, 1);

		/* Set enable bit in SPI_AR */
		MEC1322_SPI_AR(port) |= 0x1;

		/* Set SPDIN to 0 -> Full duplex */
		MEC1322_SPI_CR(port) &= ~(0x3 << 2);

		/* Set CLKPOL, TCLKPH, RCLKPH to 0 */
		MEC1322_SPI_CC(port) &= ~0x7;

		/* Set LSBF to 0 -> MSB first */
		MEC1322_SPI_CR(port) &= ~0x1;
	} else {
		/* Clear enable bit in SPI_AR */
		MEC1322_SPI_AR(port) &= ~0x1;

		gpio_config_module(MODULE_SPI, 0);
	}

	return EC_SUCCESS;
}

