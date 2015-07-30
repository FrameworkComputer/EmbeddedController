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
#include "task.h"
#include "timer.h"
#include "util.h"

/* The second (and third if available) SPI port are used as master */
static stm32_spi_regs_t *SPI_REGS[] = {
	STM32_SPI2_REGS,
#ifdef CHIP_VARIANT_STM32F373
	STM32_SPI3_REGS,
#endif
};

static struct mutex spi_mutex[ARRAY_SIZE(SPI_REGS)];

#define SPI_TRANSACTION_TIMEOUT_USEC (800 * MSEC)

/* Default DMA channel options */
static const struct dma_option dma_tx_option[] = {
	{
		STM32_DMAC_SPI2_TX, (void *)&STM32_SPI2_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
#ifdef CHIP_VARIANT_STM32F373
	{
		STM32_DMAC_SPI3_TX, (void *)&STM32_SPI3_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
#endif
};

static const struct dma_option dma_rx_option[] = {
	{
		STM32_DMAC_SPI2_RX, (void *)&STM32_SPI2_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
#ifdef CHIP_VARIANT_STM32F373
	{
		STM32_DMAC_SPI3_RX, (void *)&STM32_SPI3_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
#endif
};

static uint8_t spi_enabled[ARRAY_SIZE(SPI_REGS)];

/**
 * Initialize SPI module, registers, and clocks
 *
 * - port: which port to initialize.
 */
static int spi_master_initialize(int port)
{
	int i, div = 0;

	stm32_spi_regs_t *spi = SPI_REGS[port];

	/*
	 * Set SPI master, baud rate, and software slave control.
	 * */
	for (i = 0; i < spi_devices_used; i++)
		if ((spi_devices[i].port == port) &&
		    (div < spi_devices[i].div))
			div = spi_devices[i].div;
	spi->cr1 = STM32_SPI_CR1_MSTR | STM32_SPI_CR1_SSM | STM32_SPI_CR1_SSI |
		(div << 3);

	/*
	 * Configure 8-bit datasize, set FRXTH, enable DMA,
	 * and enable NSS output
	 */
	spi->cr2 = STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN |
			   STM32_SPI_CR2_FRXTH | STM32_SPI_CR2_DATASIZE(8);

	/* Enable SPI */
	spi->cr1 |= STM32_SPI_CR1_SPE;

	for (i = 0; i < spi_devices_used; i++) {
		if (spi_devices[i].port != port)
			continue;
		/* Drive SS high */
		gpio_set_level(spi_devices[i].gpio_cs, 1);
	}

	/* Set flag */
	spi_enabled[port] = 1;

	return EC_SUCCESS;
}

/**
 * Shutdown SPI module
 */
static int spi_master_shutdown(int port)
{
	int rv = EC_SUCCESS;

	stm32_spi_regs_t *spi = SPI_REGS[port];
	char dummy __attribute__((unused));

	/* Set flag */
	spi_enabled[port] = 0;

	/* Disable DMA streams */
	dma_disable(dma_tx_option[port].channel);
	dma_disable(dma_rx_option[port].channel);

	/* Disable SPI */
	spi->cr1 &= ~STM32_SPI_CR1_SPE;

	/* Read until FRLVL[1:0] is empty */
	while (spi->sr & STM32_SPI_SR_FTLVL)
		dummy = spi->dr;

	/* Disable DMA buffers */
	spi->cr2 &= ~(STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN);

	return rv;
}

int spi_enable(int port, int enable)
{
	if (enable == spi_enabled[port])
		return EC_SUCCESS;
	if (enable)
		return spi_master_initialize(port);
	else
		return spi_master_shutdown(port);
}

static int spi_dma_start(int port, const uint8_t *txdata,
		uint8_t *rxdata, int len)
{
	stm32_dma_chan_t *txdma;

	/* Set up RX DMA */
	dma_start_rx(&dma_rx_option[port], len, rxdata);

	/* Set up TX DMA */
	txdma = dma_get_channel(dma_tx_option[port].channel);
	dma_prepare_tx(&dma_tx_option[port], len, txdata);
	dma_go(txdma);

	return EC_SUCCESS;
}

static int spi_dma_wait(int port)
{
	timestamp_t timeout;
	stm32_spi_regs_t *spi = SPI_REGS[port];
	int rv = EC_SUCCESS;

	/* Wait for DMA transmission to complete */
	rv = dma_wait(dma_tx_option[port].channel);
	if (rv)
		return rv;

	timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
	/* Wait for FIFO empty and BSY bit clear to indicate completion */
	while ((spi->sr & STM32_SPI_SR_FTLVL) || (spi->sr & STM32_SPI_SR_BSY))
		if (get_time().val > timeout.val)
			return EC_ERROR_TIMEOUT;

	/* Disable TX DMA */
	dma_disable(dma_tx_option[port].channel);

	/* Wait for DMA reception to complete */
	rv = dma_wait(dma_rx_option[port].channel);
	if (rv)
		return rv;

	timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
	/* Wait for FRLVL[1:0] to indicate FIFO empty */
	while (spi->sr & STM32_SPI_SR_FRLVL)
		if (get_time().val > timeout.val)
			return EC_ERROR_TIMEOUT;

	/* Disable RX DMA */
	dma_disable(dma_rx_option[port].channel);

	return rv;
}

int spi_transaction_async(const struct spi_device_t *spi_device,
			  const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	int rv = EC_SUCCESS;
	int port = spi_device->port;

	stm32_spi_regs_t *spi = SPI_REGS[port];
	char *buf;

	rv = shared_mem_acquire(MAX(txlen, rxlen), &buf);
	if (rv != EC_SUCCESS)
		return rv;

	/* Drive SS low */
	gpio_set_level(spi_device->gpio_cs, 0);

	/* Clear out the FIFO. */
	while (spi->sr & STM32_SPI_SR_FRLVL)
		(void) (uint8_t) spi->dr;

	rv = spi_dma_start(port, txdata, buf, txlen);
	if (rv != EC_SUCCESS)
		goto err_free;

	rv = spi_dma_wait(port);
	if (rv != EC_SUCCESS)
		goto err_free;

	if (rxlen) {
		rv = spi_dma_start(port, buf, rxdata, rxlen);
		if (rv != EC_SUCCESS)
			goto err_free;
	}

err_free:
	shared_mem_release(buf);
	return rv;
}

int spi_transaction_flush(const struct spi_device_t *spi_device)
{
	int rv = spi_dma_wait(spi_device->port);

	/* Drive SS high */
	gpio_set_level(spi_device->gpio_cs, 1);

	return rv;
}

int spi_transaction(const struct spi_device_t *spi_device,
		    const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int rv;
	int port = spi_device->port;

	mutex_lock(spi_mutex + port);
	rv = spi_transaction_async(spi_device, txdata, txlen, rxdata, rxlen);
	rv |= spi_transaction_flush(spi_device);
	mutex_unlock(spi_mutex + port);

	return rv;
}
