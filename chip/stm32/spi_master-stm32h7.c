/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
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
#include "stm32-dma.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* SPI ports are used as master */
static stm32_spi_regs_t *SPI_REGS[] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	STM32_SPI1_REGS,
#endif
	STM32_SPI2_REGS,
	STM32_SPI3_REGS,
	STM32_SPI4_REGS,
};

/* DMA request mapping on channels */
static uint8_t dma_req_tx[ARRAY_SIZE(SPI_REGS)] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	DMAMUX1_REQ_SPI1_TX,
#endif
	DMAMUX1_REQ_SPI2_TX,
	DMAMUX1_REQ_SPI3_TX,
	DMAMUX1_REQ_SPI4_TX,
};
static uint8_t dma_req_rx[ARRAY_SIZE(SPI_REGS)] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	DMAMUX1_REQ_SPI1_RX,
#endif
	DMAMUX1_REQ_SPI2_RX,
	DMAMUX1_REQ_SPI3_RX,
	DMAMUX1_REQ_SPI4_RX,
};

static struct mutex spi_mutex[ARRAY_SIZE(SPI_REGS)];

#define SPI_TRANSACTION_TIMEOUT_USEC (800 * MSEC)

static const struct dma_option dma_tx_option[] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	{
		STM32_DMAC_SPI1_TX, (void *)&STM32_SPI1_REGS->txdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
#endif
	{
		STM32_DMAC_SPI2_TX, (void *)&STM32_SPI2_REGS->txdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
	{
		STM32_DMAC_SPI3_TX, (void *)&STM32_SPI3_REGS->txdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
	{
		STM32_DMAC_SPI4_TX, (void *)&STM32_SPI4_REGS->txdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
};

static const struct dma_option dma_rx_option[] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	{
		STM32_DMAC_SPI1_RX, (void *)&STM32_SPI1_REGS->rxdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
#endif
	{
		STM32_DMAC_SPI2_RX, (void *)&STM32_SPI2_REGS->rxdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
	{
		STM32_DMAC_SPI3_RX, (void *)&STM32_SPI3_REGS->rxdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
	{
		STM32_DMAC_SPI4_RX, (void *)&STM32_SPI4_REGS->rxdr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
	},
};

static uint8_t spi_enabled[ARRAY_SIZE(SPI_REGS)];

/**
 * Initialize SPI module, registers, and clocks
 *
 * - port: which port to initialize.
 */
static void spi_master_config(int port)
{
	int i, div = 0;

	stm32_spi_regs_t *spi = SPI_REGS[port];

	/*
	 * Set SPI master, baud rate, and software slave control.
	 */
	for (i = 0; i < spi_devices_used; i++)
		if ((spi_devices[i].port == port) &&
		    (div < spi_devices[i].div))
			div = spi_devices[i].div;

	spi->cr1 = STM32_SPI_CR1_SSI;
	spi->cfg2 = STM32_SPI_CFG2_MSTR | STM32_SPI_CFG2_SSM
			| STM32_SPI_CFG2_AFCNTR;
	spi->cfg1 = STM32_SPI_CFG1_DATASIZE(8) | STM32_SPI_CFG1_FTHLV(4)
			| STM32_SPI_CFG1_CRCSIZE(8) | STM32_SPI_CR1_DIV(div);

	dma_select_channel(dma_tx_option[port].channel, dma_req_tx[port]);
	dma_select_channel(dma_rx_option[port].channel, dma_req_rx[port]);
}

static int spi_master_initialize(int port)
{
	int i;

	spi_master_config(port);

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

	/* Set flag */
	spi_enabled[port] = 0;

	/* Disable DMA streams */
	dma_disable(dma_tx_option[port].channel);
	dma_disable(dma_rx_option[port].channel);

	/* Disable SPI */
	spi->cr1 &= ~STM32_SPI_CR1_SPE;

	/* Disable DMA buffers */
	spi->cfg1 &= ~(STM32_SPI_CFG1_TXDMAEN | STM32_SPI_CFG1_RXDMAEN);

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
	dma_chan_t *txdma;
	stm32_spi_regs_t *spi = SPI_REGS[port];

	/*
	 * Workaround for STM32H7 errata: without resetting the SPI controller,
	 * the RX DMA requests will happen too early on the 2nd transfer.
	 */
	STM32_RCC_APB2RSTR = STM32_RCC_PB2_SPI4;
	STM32_RCC_APB2RSTR = 0;
	dma_clear_isr(dma_tx_option[port].channel);
	dma_clear_isr(dma_rx_option[port].channel);
	/* restore proper SPI configuration registers. */
	spi_master_config(port);

	spi->cr2 = len;
	spi->cfg1 |= STM32_SPI_CFG1_RXDMAEN;
	/* Set up RX DMA */
	if (rxdata)
		dma_start_rx(&dma_rx_option[port], len, rxdata);

	/* Set up TX DMA */
	if (txdata) {
		txdma = dma_get_channel(dma_tx_option[port].channel);
		dma_prepare_tx(&dma_tx_option[port], len, txdata);
		dma_go(txdma);
	}

	spi->cfg1 |= STM32_SPI_CFG1_TXDMAEN;
	spi->cr1 |= STM32_SPI_CR1_SPE;
	spi->cr1 |= STM32_SPI_CR1_CSTART;

	return EC_SUCCESS;
}

static inline bool dma_is_enabled_(const struct dma_option *option)
{
	return dma_is_enabled(dma_get_channel(option->channel));
}

static int spi_dma_wait(int port)
{
	timestamp_t timeout;
	stm32_spi_regs_t *spi = SPI_REGS[port];
	int rv = EC_SUCCESS;

	/* Wait for DMA transmission to complete */
	if (dma_is_enabled_(&dma_tx_option[port])) {
		rv = dma_wait(dma_tx_option[port].channel);
		if (rv)
			return rv;

		timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
		/* Wait for FIFO empty and BSY bit clear */
		while (!(spi->sr & (STM32_SPI_SR_TXC)))
			if (get_time().val > timeout.val)
				return EC_ERROR_TIMEOUT;

		/* Disable TX DMA */
		dma_disable(dma_tx_option[port].channel);
	}

	/* Wait for DMA reception to complete */
	if (dma_is_enabled_(&dma_rx_option[port])) {
		rv = dma_wait(dma_rx_option[port].channel);
		if (rv)
			return rv;

		timeout.val = get_time().val + SPI_TRANSACTION_TIMEOUT_USEC;
		/* Wait for FRLVL[1:0] to indicate FIFO empty */
		while (spi->sr & (STM32_SPI_SR_FRLVL | STM32_SPI_SR_RXNE))
			if (get_time().val > timeout.val)
				return EC_ERROR_TIMEOUT;

		/* Disable RX DMA */
		dma_disable(dma_rx_option[port].channel);
	}

	spi->cr1 &= ~STM32_SPI_CR1_SPE;
	spi->cfg1 &= ~(STM32_SPI_CFG1_TXDMAEN | STM32_SPI_CFG1_RXDMAEN);

	return rv;
}

int spi_transaction_async(const struct spi_device_t *spi_device,
			  const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	int rv = EC_SUCCESS;
	int port = spi_device->port;
	int full_readback = 0;

	char *buf = NULL;

#ifndef CONFIG_SPI_HALFDUPLEX
	if (rxlen == SPI_READBACK_ALL) {
		buf = rxdata;
		full_readback = 1;
	} else {
		rv = shared_mem_acquire(MAX(txlen, rxlen), &buf);
		if (rv != EC_SUCCESS)
			return rv;
	}
#endif

	/* Drive SS low */
	gpio_set_level(spi_device->gpio_cs, 0);

	rv = spi_dma_start(port, txdata, buf, txlen);
	if (rv != EC_SUCCESS)
		goto err_free;

	if (full_readback)
		return EC_SUCCESS;

	if (rxlen) {
		rv = spi_dma_wait(port);
		if (rv != EC_SUCCESS)
			goto err_free;

		rv = spi_dma_start(port, buf, rxdata, rxlen);
		if (rv != EC_SUCCESS)
			goto err_free;
	}

err_free:
	if (!full_readback)
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

int spi_transaction_wait(const struct spi_device_t *spi_device)
{
	return spi_dma_wait(spi_device->port);
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
