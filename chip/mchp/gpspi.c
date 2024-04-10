/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* General Purpose SPI master module for MCHP MEC */

#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "gpspi_chip.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "spi_chip.h"
#include "task.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ##args)

#define SPI_BYTE_TRANSFER_TIMEOUT_US (3 * MSEC)
/* One byte at 12 MHz full duplex = 0.67 us */
#define SPI_BYTE_TRANSFER_POLL_INTERVAL_US 20

/*
 * GP-SPI
 */

/**
 * Return zero based GPSPI controller index given hardware port.
 * @param hw_port b[7:4]==1 (GPSPI), b[3:0]=0(GPSPI0), 1(GPSPI1)
 * @return 0(GPSPI0) or 1(GPSPI1)
 */
static uint8_t gpspi_port_to_ctrl_id(uint8_t hw_port)
{
	return (hw_port & 0x01);
}

static int gpspi_wait_byte(const int ctrl)
{
	timestamp_t deadline;

	deadline.val = get_time().val + SPI_BYTE_TRANSFER_TIMEOUT_US;
	while ((MCHP_SPI_SR(ctrl) & 0x3) != 0x3) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		crec_usleep(SPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}
	return EC_SUCCESS;
}

/* NOTE: auto-read must be disabled before calling this routine! */
static void gpspi_rx_fifo_clean(const int ctrl)
{
	__maybe_unused uint8_t unused = 0;

	/* If ACTIVE and/or RXFF then clean it */
	if ((MCHP_SPI_SR(ctrl) & 0x4) == 0x4)
		unused += MCHP_SPI_RD(ctrl);

	if ((MCHP_SPI_SR(ctrl) & 0x2) == 0x2)
		unused += MCHP_SPI_RD(ctrl);
}
/*
 * NOTE: auto-read must be disabled before calling this routine!
 */
#ifndef CONFIG_MCHP_GPSPI_TX_DMA
static int gpspi_tx(const int ctrl, const uint8_t *txdata, int txlen)
{
	int i;
	int ret;
	uint8_t unused = 0;

	gpspi_rx_fifo_clean(ctrl);

	ret = EC_SUCCESS;
	for (i = 0; i < txlen; ++i) {
		MCHP_SPI_TD(ctrl) = txdata[i];
		ret = gpspi_wait_byte(ctrl);
		if (ret != EC_SUCCESS)
			break;
		unused += MCHP_SPI_RD(ctrl);
	}

	return ret;
}
#endif

int gpspi_transaction_async(const struct spi_device_t *spi_device,
			    const uint8_t *txdata, int txlen, uint8_t *rxdata,
			    int rxlen)
{
	int hw_port, ctrl;
	int ret = EC_SUCCESS;
	int cs_asserted = 0;
	const struct dma_option *opdma;
#ifdef CONFIG_MCHP_GPSPI_TX_DMA
	dma_chan_t *chan;
#endif
	if (spi_device == NULL)
		return EC_ERROR_PARAM1;

	hw_port = spi_device->port;

	ctrl = gpspi_port_to_ctrl_id(hw_port);

	/* Disable auto read */
	MCHP_SPI_CR(ctrl) &= ~BIT(5);

	if ((txdata != NULL) && (txdata != 0)) {
#ifdef CONFIG_MCHP_GPSPI_TX_DMA
		opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_WR);
		if (opdma == NULL)
			return EC_ERROR_INVAL;

		gpspi_rx_fifo_clean(ctrl);

		dma_prepare_tx(opdma, txlen, txdata);

		chan = dma_get_channel(opdma->channel);

		gpio_set_level(spi_device->gpio_cs, 0);
		cs_asserted = 1;

		dma_go(chan);
		ret = dma_wait(opdma->channel);
		if (ret == EC_SUCCESS)
			ret = gpspi_wait_byte(ctrl);

		dma_disable(opdma->channel);
		dma_clear_isr(opdma->channel);

		gpspi_rx_fifo_clean(ctrl);
#else
		gpio_set_level(spi_device->gpio_cs, 0);
		cs_asserted = 1;

		ret = gpspi_tx(ctrl, txdata, txlen);
#endif
	}

	if (ret == EC_SUCCESS)
		if ((rxlen != 0) && (rxdata != NULL)) {
			ret = EC_ERROR_INVAL;
			opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);
			if (opdma != NULL) {
				if (!cs_asserted)
					gpio_set_level(spi_device->gpio_cs, 0);
				/* Enable auto read */
				MCHP_SPI_CR(ctrl) |= BIT(5);
				dma_start_rx(opdma, rxlen, rxdata);
				MCHP_SPI_TD(ctrl) = 0;
				ret = EC_SUCCESS;
			}
		}

	return ret;
}

int gpspi_transaction_flush(const struct spi_device_t *spi_device)
{
	int ctrl, hw_port;
	int ret;
	enum dma_channel chan;
	const struct dma_option *opdma;
	timestamp_t deadline;

	if (spi_device == NULL)
		return EC_ERROR_PARAM1;

	hw_port = spi_device->port;
	ctrl = gpspi_port_to_ctrl_id(hw_port);
	opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);
	chan = opdma->channel;

	ret = dma_wait(chan);

	/* Disable auto read */
	MCHP_SPI_CR(ctrl) &= ~BIT(5);

	deadline.val = get_time().val + SPI_BYTE_TRANSFER_TIMEOUT_US;
	/* Wait for FIFO empty SPISR_TXBE */
	while ((MCHP_SPI_SR(ctrl) & 0x01) != 0x1) {
		if (timestamp_expired(deadline, NULL)) {
			ret = EC_ERROR_TIMEOUT;
			break;
		}
		crec_usleep(SPI_BYTE_TRANSFER_POLL_INTERVAL_US);
	}

	dma_disable(chan);
	dma_clear_isr(chan);
	if (MCHP_SPI_SR(ctrl) & 0x2)
		hw_port = MCHP_SPI_RD(ctrl);

	gpio_set_level(spi_device->gpio_cs, 1);

	return ret;
}

int gpspi_transaction_wait(const struct spi_device_t *spi_device)
{
	const struct dma_option *opdma;

	opdma = spi_dma_option(spi_device, SPI_DMA_OPTION_RD);

	return dma_wait(opdma->channel);
}

/**
 * Enable GPSPI controller and MODULE_SPI_CONTROLLER pins
 *
 * @param hw_port b[7:4]=1 b[3:0]=0(GPSPI0), 1(GPSPI1)
 * @param enable
 * @return EC_SUCCESS or EC_ERROR_INVAL if port is unrecognized
 * @note called from mec1701/spi.c
 *
 */
int gpspi_enable(int hw_port, int enable)
{
	uint32_t ctrl;

	if ((hw_port != GPSPI0_PORT) && (hw_port != GPSPI1_PORT))
		return EC_ERROR_INVAL;

	gpio_config_module(MODULE_SPI_CONTROLLER, (enable > 0));

	ctrl = (uint32_t)hw_port & 0x0f;

	if (enable) {
		if (ctrl)
			MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_GPSPI1);
		else
			MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_GPSPI0);

		/* Set enable bit in SPI_AR */
		MCHP_SPI_AR(ctrl) |= 0x1;

		/* Set SPDIN to 0 -> Full duplex */
		MCHP_SPI_CR(ctrl) &= ~(0x3 << 2);

		/* Set CLKPOL, TCLKPH, RCLKPH to 0 */
		MCHP_SPI_CC(ctrl) &= ~0x7;

		/* Set LSBF to 0 -> MSB first */
		MCHP_SPI_CR(ctrl) &= ~0x1;
	} else {
		/* soft reset */
		MCHP_SPI_CR(ctrl) |= (1u << 4);

		/* Clear enable bit in SPI_AR */
		MCHP_SPI_AR(ctrl) &= ~0x1;

		if (ctrl)
			MCHP_PCR_SLP_EN_DEV(MCHP_PCR_GPSPI1);
		else
			MCHP_PCR_SLP_EN_DEV(MCHP_PCR_GPSPI0);
	}

	return EC_SUCCESS;
}
