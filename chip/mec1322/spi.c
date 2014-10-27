/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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

#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)

#define SPI_BYTE_TRANSFER_TIMEOUT_US (3 * MSEC)
#define SPI_BYTE_TRANSFER_POLL_INTERVAL_US 100

#define SPI_DMA_CHANNEL (MEC1322_DMAC_SPI0_RX + CONFIG_SPI_PORT * 2)

static const struct dma_option spi_rx_option = {
	SPI_DMA_CHANNEL, (void *)&MEC1322_SPI_RD(CONFIG_SPI_PORT),
	MEC1322_DMA_XFER_SIZE(1)
};

static int wait_byte(void)
{
	timestamp_t deadline;

	deadline.val = get_time().val + SPI_BYTE_TRANSFER_TIMEOUT_US;
	while ((MEC1322_SPI_SR(CONFIG_SPI_PORT) & 0x3) != 0x3) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		usleep(SPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}
	return EC_SUCCESS;
}

static int spi_tx(const uint8_t *txdata, int txlen)
{
	int i;
	int ret = EC_SUCCESS;
	uint8_t dummy __attribute__((unused)) = 0;

	for (i = 0; i < txlen; ++i) {
		MEC1322_SPI_TD(CONFIG_SPI_PORT) = txdata[i];
		ret = wait_byte();
		if (ret != EC_SUCCESS)
			return ret;
		dummy = MEC1322_SPI_RD(CONFIG_SPI_PORT);
	}

	return ret;
}

int spi_transaction_async(const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	int ret = EC_SUCCESS;

	gpio_set_level(CONFIG_SPI_CS_GPIO, 0);

	/* Disable auto read */
	MEC1322_SPI_CR(CONFIG_SPI_PORT) &= ~(1 << 5);

	ret = spi_tx(txdata, txlen);
	if (ret != EC_SUCCESS)
		return ret;

	/* Enable auto read */
	MEC1322_SPI_CR(CONFIG_SPI_PORT) |= 1 << 5;

	if (rxlen != 0) {
		dma_start_rx(&spi_rx_option, rxlen, rxdata);
		MEC1322_SPI_TD(CONFIG_SPI_PORT) = 0;
	}
	return ret;
}

int spi_transaction_flush(void)
{
	int ret = dma_wait(SPI_DMA_CHANNEL);
	uint8_t dummy __attribute__((unused)) = 0;

	timestamp_t deadline;

	/* Disable auto read */
	MEC1322_SPI_CR(CONFIG_SPI_PORT) &= ~(1 << 5);

	deadline.val = get_time().val + SPI_BYTE_TRANSFER_TIMEOUT_US;
	/* Wait for FIFO empty SPISR_TXBE */
	while ((MEC1322_SPI_SR(CONFIG_SPI_PORT) & 0x01) != 0x1) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		usleep(SPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}

	dma_disable(SPI_DMA_CHANNEL);
	dma_clear_isr(SPI_DMA_CHANNEL);
	if (MEC1322_SPI_SR(CONFIG_SPI_PORT) & 0x2)
		dummy = MEC1322_SPI_RD(CONFIG_SPI_PORT);

	gpio_set_level(CONFIG_SPI_CS_GPIO, 1);

	return ret;
}

int spi_transaction(const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int ret;

	ret = spi_transaction_async(txdata, txlen, rxdata, rxlen);
	if (ret)
		return ret;
	return spi_transaction_flush();
}

int spi_enable(int enable)
{
	if (enable) {
		gpio_config_module(MODULE_SPI, 1);

		/* Set enable bit in SPI_AR */
		MEC1322_SPI_AR(CONFIG_SPI_PORT) |= 0x1;

		/* Set SPDIN to 0 -> Full duplex */
		MEC1322_SPI_CR(CONFIG_SPI_PORT) &= ~(0x3 << 2);

		/* Set CLKPOL, TCLKPH, RCLKPH to 0 */
		MEC1322_SPI_CC(CONFIG_SPI_PORT) &= ~0x7;

		/* Set LSBF to 0 -> MSB first */
		MEC1322_SPI_CR(CONFIG_SPI_PORT) &= ~0x1;
	} else {
		/* Clear enable bit in SPI_AR */
		MEC1322_SPI_AR(CONFIG_SPI_PORT) &= ~0x1;

		gpio_config_module(MODULE_SPI, 0);
	}

	return EC_SUCCESS;
}

