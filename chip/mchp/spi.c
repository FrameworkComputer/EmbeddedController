/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* QMSPI master module for MCHP MEC family */

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
#include "spi_chip.h"
#include "qmspi_chip.h"
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
#include "gpspi_chip.h"
#endif
#include "tfdp_chip.h"

#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)

#define SPI_BYTE_TRANSFER_TIMEOUT_US (3 * MSEC)
#define SPI_BYTE_TRANSFER_POLL_INTERVAL_US 100



static const struct dma_option spi_rx_option[] = {
	{
		MCHP_DMAC_QMSPI0_RX,
		(void *)(MCHP_QMSPI0_RX_FIFO_ADDR),
		MCHP_DMA_XFER_SIZE(1) + MCHP_DMA_INC_MEM
	},
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
#if CONFIG_MCHP_GPSPI & 0x01
	{
		MCHP_DMAC_SPI0_RX,
		(void *)&MCHP_SPI_RD(0),
		MCHP_DMA_XFER_SIZE(1) + MCHP_DMA_INC_MEM
	},
#endif
#if CONFIG_MCHP_GPSPI & 0x02
	{
		MCHP_DMAC_SPI1_RX,
		(void *)&MCHP_SPI_RD(1),
		MCHP_DMA_XFER_SIZE(1) + MCHP_DMA_INC_MEM
	},
#endif
#endif
};

static const struct dma_option spi_tx_option[] = {
	{
		MCHP_DMAC_QMSPI0_TX,
		(void *)(MCHP_QMSPI0_TX_FIFO_ADDR),
		MCHP_DMA_XFER_SIZE(1) + MCHP_DMA_INC_MEM
	},
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
#if CONFIG_MCHP_GPSPI & 0x01
	{
		MCHP_DMAC_SPI0_TX,
		(void *)&MCHP_SPI_TD(0),
		MCHP_DMA_XFER_SIZE(1) + MCHP_DMA_INC_MEM
	},
#endif
#if CONFIG_MCHP_GPSPI & 0x02
	{
		MCHP_DMAC_SPI1_TX,
		(void *)&MCHP_SPI_TD(1),
		MCHP_DMA_XFER_SIZE(1) + MCHP_DMA_INC_MEM
	},
#endif
#endif
};

/* only regular image needs mutex, LFW does not have scheduling */
#ifndef LFW
static struct mutex spi_mutex[ARRAY_SIZE(spi_rx_option)];

/*
 * Acquire mutex for specified SPI controller/port.
 * Note if mutex is owned by another task this routine
 * will block until mutex is released.
 */
static void spi_mutex_lock(uint8_t hw_port)
{
	uint32_t n;

	n = 0;
#ifdef CONFIG_MCHP_GPSPI
	if (hw_port & 0xF0) {
#if (CONFIG_MCHP_GPSPI & 0x03) == 0x03
		n = (hw_port & 0x0F) + 1;
#else
		n = 1;
#endif
	}
#endif
	mutex_lock(&spi_mutex[n]);
}

/*
 * Release mutex for specified SPI controller/port.
 */
static void spi_mutex_unlock(uint8_t hw_port)
{
	uint32_t n;

	n = 0;
#ifdef CONFIG_MCHP_GPSPI
	if (hw_port & 0xF0) {
#if (CONFIG_MCHP_GPSPI & 0x03) == 0x03
		n = (hw_port & 0x0F) + 1;
#else
		n = 1;
#endif
	}
#endif
	mutex_unlock(&spi_mutex[n]);
}
#endif /* #ifndef LFW */

/*
 * Public SPI interface
 */

const void *spi_dma_option(const struct spi_device_t *spi_device,
				int is_tx)
{
	uint32_t n;

	if (spi_device == NULL)
		return NULL;

	n = 0;
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
	if (spi_device->port & 0xF0) {
#if (CONFIG_MCHP_GPSPI & 0x03) == 0x03
		n = (spi_device->port & 0x0F) + 1;
#else
		n = 1;
#endif
	}
#endif

	if (is_tx)
		return &spi_tx_option[n];
	else
		return &spi_rx_option[n];
}

int spi_transaction_async(const struct spi_device_t *spi_device,
				const uint8_t *txdata, int txlen,
				uint8_t *rxdata, int rxlen)
{
	int rc;

	if (spi_device == NULL)
		return EC_ERROR_INVAL;

	switch (spi_device->port) {
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
	case GPSPI0_PORT:
	case GPSPI1_PORT:
		rc = gpspi_transaction_async(spi_device, txdata,
				txlen, rxdata, rxlen);
		break;
#endif
	case QMSPI0_PORT:
		rc = qmspi_transaction_async(spi_device, txdata,
				txlen, rxdata, rxlen);
		break;
	default:
		rc = EC_ERROR_INVAL;
	}

	return rc;
}

int spi_transaction_flush(const struct spi_device_t *spi_device)
{
	int rc;

	if (spi_device == NULL)
		return EC_ERROR_INVAL;

	switch (spi_device->port) {
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
	case GPSPI0_PORT:
	case GPSPI1_PORT:
		rc = gpspi_transaction_flush(spi_device);
		break;
#endif
	case QMSPI0_PORT:
		rc = qmspi_transaction_flush(spi_device);
		break;
	default:
		rc = EC_ERROR_INVAL;
	}

	return rc;
}

/* Wait for async response received but do not de-assert chip select */
int spi_transaction_wait(const struct spi_device_t *spi_device)
{
	int rc;

	if (spi_device == NULL)
		return EC_ERROR_INVAL;

	switch (spi_device->port) {
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
#ifndef LFW
	case GPSPI0_PORT:
	case GPSPI1_PORT:
		rc = gpspi_transaction_wait(spi_device);
		break;
#endif
#endif
	case QMSPI0_PORT:
		rc = qmspi_transaction_wait(spi_device);
		break;
	default:
		rc = EC_ERROR_INVAL;
	}

	return rc;
}

/*
 * called from common/spi_flash.c
 * For tranfers reading less than the size of QMSPI RX FIFO call
 * a routine where reads use FIFO only no DMA.
 * GP-SPI only has a one byte RX FIFO but small data transfers will be OK
 * without the overhead of DMA setup.
 */
int spi_transaction(const struct spi_device_t *spi_device,
		    const uint8_t *txdata, int txlen,
		    uint8_t *rxdata, int rxlen)
{
	int rc;

	if (spi_device == NULL)
		return EC_ERROR_PARAM1;

#ifndef LFW
	spi_mutex_lock(spi_device->port);
#endif

	rc = spi_transaction_async(spi_device, txdata, txlen, rxdata, rxlen);
	if (rc == EC_SUCCESS)
		rc = spi_transaction_flush(spi_device);

#ifndef LFW
	spi_mutex_unlock(spi_device->port);
#endif

	return rc;
}

/**
 * Enable SPI port and associated controller
 *
 * @param port Zero based index into spi_device an array of
 *             struct spi_device_t
 * @param enable
 * @return EC_SUCCESS or EC_ERROR_INVAL if port is unrecognized
 * @note called from common/spi_flash.c
 *
 * spi_devices[].port is defined as
 * bits[3:0] = controller instance
 * bits[7:4] = controller family 0 = QMSPI, 1 = GPSPI
 */
int spi_enable(int port, int enable)
{
	int rc;
	uint8_t hw_port;

	rc = EC_ERROR_INVAL;
	if (port < spi_devices_used) {
		hw_port = spi_devices[port].port;
		if ((hw_port & 0xF0) == QMSPI_CLASS)
			rc = qmspi_enable(hw_port, enable);
#if defined(CONFIG_MCHP_GPSPI) && !defined(LFW)
		if ((hw_port & 0xF0) == GPSPI_CLASS)
			rc = gpspi_enable(hw_port, enable);
#endif
	}

	return rc;
}
