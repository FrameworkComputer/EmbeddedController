/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI master driver.
 */

#include "common.h"
#include "dma.h"
#include "gpio.h"
#include "hwtimer.h"
#include "shared_mem.h"
#include "spi.h"
#include "stm32-dma.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#if defined(CHIP_VARIANT_STM32F373)  || \
	defined(CHIP_FAMILY_STM32L4) || \
	defined(CHIP_VARIANT_STM32F76X)
#define HAS_SPI3
#else
#undef  HAS_SPI3
#endif

/* The second (and third if available) SPI port are used as master */
static stm32_spi_regs_t *SPI_REGS[] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	STM32_SPI1_REGS,
#endif
	STM32_SPI2_REGS,
#ifdef HAS_SPI3
	STM32_SPI3_REGS,
#endif
};

#ifdef CHIP_FAMILY_STM32L4
/* DMA request mapping on channels */
static uint8_t dma_req[ARRAY_SIZE(SPI_REGS)] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	/* SPI1 */ 1,
#endif
	/* SPI2 */ 1,
	/* SPI3 */ 3,
};
#endif

static struct mutex spi_mutex[ARRAY_SIZE(SPI_REGS)];

#define SPI_TRANSACTION_TIMEOUT_USEC (800 * MSEC)

/* Default DMA channel options */
#ifdef CHIP_FAMILY_STM32F4
#define F4_CHANNEL(ch)	STM32_DMA_CCR_CHANNEL(ch)
#else
#define F4_CHANNEL(ch)	0
#endif

static const struct dma_option dma_tx_option[] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	{
		STM32_DMAC_SPI1_TX, (void *)&STM32_SPI1_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
		| F4_CHANNEL(STM32_SPI1_TX_REQ_CH)
	},
#endif
	{
		STM32_DMAC_SPI2_TX, (void *)&STM32_SPI2_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
		| F4_CHANNEL(STM32_SPI2_TX_REQ_CH)
	},
#ifdef HAS_SPI3
	{
		STM32_DMAC_SPI3_TX, (void *)&STM32_SPI3_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
		| F4_CHANNEL(STM32_SPI3_TX_REQ_CH)
	},
#endif
};

static const struct dma_option dma_rx_option[] = {
#ifdef CONFIG_STM32_SPI1_MASTER
	{
		STM32_DMAC_SPI1_RX, (void *)&STM32_SPI1_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
		| F4_CHANNEL(STM32_SPI1_RX_REQ_CH)
	},
#endif
	{
		STM32_DMAC_SPI2_RX, (void *)&STM32_SPI2_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
		| F4_CHANNEL(STM32_SPI2_RX_REQ_CH)
	},
#ifdef HAS_SPI3
	{
		STM32_DMAC_SPI3_RX, (void *)&STM32_SPI3_REGS->dr,
		STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT
		| F4_CHANNEL(STM32_SPI3_RX_REQ_CH)
	},
#endif
};

static uint8_t spi_enabled[ARRAY_SIZE(SPI_REGS)];

static int spi_tx_done(stm32_spi_regs_t *spi)
{
	return !(spi->sr & (STM32_SPI_SR_FTLVL | STM32_SPI_SR_BSY));
}

static int spi_rx_done(stm32_spi_regs_t *spi)
{
	return !(spi->sr & (STM32_SPI_SR_FRLVL | STM32_SPI_SR_RXNE));
}

/* Read until RX FIFO is empty (i.e. RX done) */
static int spi_clear_rx_fifo(stm32_spi_regs_t *spi)
{
	uint8_t unused __attribute__((unused));
	uint32_t start = __hw_clock_source_read(), delta;

	while (!spi_rx_done(spi)) {
		unused = spi->dr;  /* Read one byte from FIFO */
		delta = __hw_clock_source_read() - start;
		if (delta >= SPI_TRANSACTION_TIMEOUT_USEC)
			return EC_ERROR_TIMEOUT;
	}
	return EC_SUCCESS;
}

/* Wait until TX FIFO is empty (i.e. TX done) */
static int spi_clear_tx_fifo(stm32_spi_regs_t *spi)
{
	uint32_t start = __hw_clock_source_read(), delta;

	while (!spi_tx_done(spi)) {
		/* wait for TX complete */
		delta = __hw_clock_source_read() - start;
		if (delta >= SPI_TRANSACTION_TIMEOUT_USEC)
			return EC_ERROR_TIMEOUT;
	}
	return EC_SUCCESS;
}

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

	/*
	 * STM32F412
	 * Section 26.3.5 Slave select (NSS) pin management and Figure 276
	 * https://www.st.com/resource/en/reference_manual/dm00180369.pdf#page=817
	 *
	 * The documentation in this section is a bit confusing, so here's a
	 * summary based on discussion with ST:
	 *
	 * Software NSS management (SSM = 1):
	 *   - In master mode, the NSS output is deactivated. You need to use a
	 *     GPIO in output mode for slave select. This is generally used for
	 *     multi-slave operation, but you can also use it for single slave
	 *     operation. In this case, you should make sure to configure a GPIO
	 *     for NSS, but *not* activate the SPI alternate function on that
	 *     same pin since that will enable hardware NSS management (see
	 *     below).
	 *   - In slave mode, the NSS input level is equal to the SSI bit value.
	 *
	 * Hardware NSS management (SSM = 0):
	 *   - In slave mode, when NSS pin is detected low the slave (MCU) is
	 *     selected.
	 *   - In master mode, there are two configurations, depending on the
	 *     SSOE bit in register SPIx_CR1.
	 *       - NSS output enable (SSM=0, SSOE=1):
	 *         The MCU (master) drives NSS low as soon as SPI is enabled
	 *         (SPE=1) and releases it when SPI is disabled (SPE=0).
	 *
	 *       - NSS output disable (SSM=0, SSOE=0):
	 *         Allows multimaster capability. The MCU (master) drives NSS
	 *         low.  If another master tries to takes control of the bus and
	 *         NSS is pulled low, a mode fault is generated and the MCU
	 *         changes to slave mode.
	 *
	 *   - NSS output disable (SSM=0, SSOE=0): if the MCU is acting as
	 *     master on the bus, this config allows multimaster capability. If
	 *     the NSS pin is pulled low in this mode, the SPI enters master
	 *     mode fault state and the device is automatically reconfigured in
	 *     slave mode.  In slave mode, the NSS pin works as a standard "chip
	 *     select" input and the slave is selected while NSS lin is at low
	 *     level.
	 */
	spi->cr1 = STM32_SPI_CR1_MSTR | STM32_SPI_CR1_SSM | STM32_SPI_CR1_SSI |
		(div << 3);

#ifdef CHIP_FAMILY_STM32L4
	dma_select_channel(dma_tx_option[port].channel, dma_req[port]);
	dma_select_channel(dma_rx_option[port].channel, dma_req[port]);
#endif
	/*
	 * Configure 8-bit datasize, set FRXTH, enable DMA,
	 * and set data size (applies to STM32F0 only).
	 *
	 * STM32F412:
	 * https://www.st.com/resource/en/reference_manual/dm00180369.pdf#page=852
	 *
	 *
	 * STM32F0:
	 * https://www.st.com/resource/en/reference_manual/dm00031936.pdf#page=803
	 */
	spi->cr2 = STM32_SPI_CR2_TXDMAEN | STM32_SPI_CR2_RXDMAEN |
			STM32_SPI_CR2_FRXTH | STM32_SPI_CR2_DATASIZE(8);

#ifdef CONFIG_SPI_HALFDUPLEX
	spi->cr1 |= STM32_SPI_CR1_BIDIMODE | STM32_SPI_CR1_BIDIOE;
#endif

	/* Drive Chip Select high for all ports before turning on SPI module */
	for (i = 0; i < spi_devices_used; i++) {
		if (spi_devices[i].port != port)
			continue;
		gpio_set_level(spi_devices[i].gpio_cs, 1);
	}

	/* Enable SPI hardware module. This will actively drive the CLK pin */
	spi->cr1 |= STM32_SPI_CR1_SPE;

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

	/* Disable SPI. Let the CLK pin float. */
	spi->cr1 &= ~STM32_SPI_CR1_SPE;

	spi_clear_rx_fifo(spi);

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
	dma_chan_t *txdma;

	/* Set up RX DMA */
	if (rxdata)
		dma_start_rx(&dma_rx_option[port], len, rxdata);

	/* Set up TX DMA */
	if (txdata) {
		txdma = dma_get_channel(dma_tx_option[port].channel);
		dma_prepare_tx(&dma_tx_option[port], len, txdata);
		dma_go(txdma);
	}

	return EC_SUCCESS;
}

static bool dma_is_enabled_(const struct dma_option *option)
{
	return dma_is_enabled(dma_get_channel(option->channel));
}

static int spi_dma_wait(int port)
{
	int rv = EC_SUCCESS;

	/* Wait for DMA transmission to complete */
	if (dma_is_enabled_(&dma_tx_option[port])) {
		/*
		 * In TX mode, SPI only generates clock when we write to FIFO.
		 * Therefore, even though `dma_wait` polls with interval 0.1ms,
		 * we won't send extra bytes.
		 */
		rv = dma_wait(dma_tx_option[port].channel);
		if (rv)
			return rv;
		/* Disable TX DMA */
		dma_disable(dma_tx_option[port].channel);
	}

	/* Wait for DMA reception to complete */
	if (dma_is_enabled_(&dma_rx_option[port])) {
		/*
		 * Because `dma_wait` polls with interval 0.1ms, we will read at
		 * least ~100 bytes (with 8MHz clock).  If you don't want this
		 * overhead, you can use interrupt handler
		 * (`dma_enable_tc_interrupt_callback`) and disable SPI
		 * interface in callback function.
		 */
		rv = dma_wait(dma_rx_option[port].channel);
		if (rv)
			return rv;
		/* Disable RX DMA */
		dma_disable(dma_rx_option[port].channel);
	}
	return rv;
}

int spi_transaction_async(const struct spi_device_t *spi_device,
			  const uint8_t *txdata, int txlen,
			  uint8_t *rxdata, int rxlen)
{
	int rv = EC_SUCCESS;
	int port = spi_device->port;
	int full_readback = 0;

	stm32_spi_regs_t *spi = SPI_REGS[port];
	char *buf = NULL;

	/* We should not ever be called when disabled, but fail early if so. */
	if (!spi_enabled[port])
		return EC_ERROR_BUSY;

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

	spi_clear_rx_fifo(spi);

	rv = spi_dma_start(port, txdata, buf, txlen);
	if (rv != EC_SUCCESS)
		goto err_free;

#ifdef CONFIG_SPI_HALFDUPLEX
	spi->cr1 |= STM32_SPI_CR1_BIDIOE;
#endif

	if (full_readback)
		return EC_SUCCESS;

	rv = spi_dma_wait(port);
	if (rv != EC_SUCCESS)
		goto err_free;

	spi_clear_tx_fifo(spi);

	if (rxlen) {
		rv = spi_dma_start(port, buf, rxdata, rxlen);
		if (rv != EC_SUCCESS)
			goto err_free;
#ifdef CONFIG_SPI_HALFDUPLEX
		spi->cr1 &= ~STM32_SPI_CR1_BIDIOE;
#endif
	}

err_free:
#ifndef CONFIG_SPI_HALFDUPLEX
	if (!full_readback)
		shared_mem_release(buf);
#endif
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
