/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI master driver.
 */

#include "common.h"
#include "dma.h"
#include "gpio.h"
#include "shared_mem.h"
#include "spi.h"
#include "timer.h"
#include "util.h"

#define SPI_REG_ADDR CONCAT3(STM32_SPI, CONFIG_SPI_MASTER_PORT, _BASE)
#define SPI_REG ((stm32_spi_regs_t *)SPI_REG_ADDR)
#define DMAC_SPI_TX CONCAT3(STM32_DMAC_SPI, CONFIG_SPI_MASTER_PORT, _TX)
#define DMAC_SPI_RX CONCAT3(STM32_DMAC_SPI, CONFIG_SPI_MASTER_PORT, _RX)

#define SPI_TRANSACTION_TIMEOUT_USEC (800 * MSEC)

/* Default DMA channel options */
static const struct dma_option dma_tx_option = {
	DMAC_SPI_TX, (void *)&SPI_REG->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
};

static const struct dma_option dma_rx_option = {
	DMAC_SPI_RX, (void *)&SPI_REG->dr,
	STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
};

static uint8_t spi_enabled;

/**
 * Initialize SPI module, registers, and clocks
 */
static int spi_master_initialize(void)
{
	stm32_spi_regs_t *spi = SPI_REG;

	/*
	 * Set SPI master, baud rate, and software slave control.
	 * Set SPI clock rate to DIV2R = 24 MHz
	 * */
	spi->cr1 = STM32_SPI_CR1_MSTR | STM32_SPI_CR1_SSM | STM32_SPI_CR1_SSI;

	/*
	 * Configure 8-bit datasize, set FRXTH, enable DMA,
	 * and enable NSS output
	 */
	spi->cr2 = STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN |
			   STM32_SPI_CR2_FRXTH | STM32_SPI_CR2_DATASIZE(8);

	/* Enable SPI */
	spi->cr1 |= STM32_SPI_CR1_SPE;

	/* Drive SS high */
	gpio_set_level(CONFIG_SPI_CS_GPIO, 1);

	/* Set flag */
	spi_enabled = 1;

	return EC_SUCCESS;
}

/**
 * Shutdown SPI module
 */
static int spi_master_shutdown(void)
{
	int rv = EC_SUCCESS;
	stm32_spi_regs_t *spi = SPI_REG;
	char dummy __attribute__((unused));

	/* Set flag */
	spi_enabled = 0;

	/* Disable DMA streams */
	dma_disable(dma_tx_option.channel);
	dma_disable(dma_rx_option.channel);

	/* Disable SPI */
	spi->cr1 &= ~STM32_SPI_CR1_SPE;

	/* Read until FRLVL[1:0] is empty */
	while (spi->sr & STM32_SPI_SR_FTLVL)
		dummy = spi->dr;

	/* Disable DMA buffers */
	spi->cr2 &= ~(STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN);

	return rv;
}

int spi_enable(int enable)
{
	if (enable == spi_enabled)
		return EC_SUCCESS;
	if (enable)
		return spi_master_initialize();
	else
		return spi_master_shutdown();
}

static int spi_dma_start(const uint8_t *txdata, uint8_t *rxdata, int len)
{
	stm32_dma_chan_t *txdma;

	/* Set up RX DMA */
	dma_start_rx(&dma_rx_option, len, rxdata);

	/* Set up TX DMA */
	txdma = dma_get_channel(dma_tx_option.channel);
	dma_prepare_tx(&dma_tx_option, len, txdata);
	dma_go(txdma);

	return EC_SUCCESS;
}

static int spi_dma_wait(void)
{
	timestamp_t timeout;
	stm32_spi_regs_t *spi = SPI_REG;
	int rv = EC_SUCCESS;

	/* Wait for DMA transmission to complete */
	rv = dma_wait(dma_tx_option.channel);
	if (rv)
		return rv;

	timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
	/* Wait for FIFO empty and BSY bit clear to indicate completion */
	while ((spi->sr & STM32_SPI_SR_FTLVL) || (spi->sr & STM32_SPI_SR_BSY))
		if (get_time().val > timeout.val)
			return EC_ERROR_TIMEOUT;

	/* Disable TX DMA */
	dma_disable(dma_tx_option.channel);

	/* Wait for DMA reception to complete */
	rv = dma_wait(dma_rx_option.channel);
	if (rv)
		return rv;

	timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
	/* Wait for FRLVL[1:0] to indicate FIFO empty */
	while (spi->sr & STM32_SPI_SR_FRLVL)
		if (get_time().val > timeout.val)
			return EC_ERROR_TIMEOUT;

	/* Disable RX DMA */
	dma_disable(dma_rx_option.channel);

	return rv;
}

int spi_transaction_async(const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	int rv = EC_SUCCESS;
	stm32_spi_regs_t *spi = SPI_REG;
	char *buf;

	rv = shared_mem_acquire(MAX(txlen, rxlen), &buf);
	if (rv != EC_SUCCESS)
		return rv;

	/* Drive SS low */
	gpio_set_level(CONFIG_SPI_CS_GPIO, 0);

	/* Clear out the FIFO. */
	while (spi->sr & STM32_SPI_SR_FRLVL)
		(void) (uint8_t) spi->dr;

	rv = spi_dma_start(txdata, buf, txlen);
	if (rv != EC_SUCCESS)
		goto err_free;

	rv = spi_dma_wait();
	if (rv != EC_SUCCESS)
		goto err_free;

	if (rxlen) {
		rv = spi_dma_start(buf, rxdata, rxlen);
		if (rv != EC_SUCCESS)
			goto err_free;
	}

err_free:
	shared_mem_release(buf);
	return rv;
}

int spi_transaction_flush(void)
{
	int rv = spi_dma_wait();

	/* Drive SS high */
	gpio_set_level(CONFIG_SPI_CS_GPIO, 1);

	return rv;
}

int spi_transaction(const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int rv;

	rv = spi_transaction_async(txdata, txlen, rxdata, rxlen);
	rv |= spi_transaction_flush();

	return rv;
}
