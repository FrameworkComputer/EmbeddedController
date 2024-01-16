/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "MCHP_MEC172x.h"
#include "common.h"
#include "failure_response.h"
#include "gpio.h"
#include "spi_flash.h"

#include <stdint.h>

#define PAGE_SIZE 256
#define HANDSHAKE_TIMEOUT_LONG 1000000
#define HANDSHAKE_TIMEOUT 100000
#define TIMEOUT_870MS 1

#define WREN_CMD 0x06
#define ERASE_SECTOR 0x20
#define FAST_READ 0x0B
#define READ_STATUS 0x05
#define PAGE_PROGRAM 0x02
#define RSTEN 0x66
#define SPI_RST 0x99
#define GLOBAL_UNLOCK_CMD 0x98

#define WEL_BIT (1 << 1)

/*
 * QSPI timeout s/w loop takes ~ 2.92 usec So, 200sec/0.00000292sec = 69000000.
 * 69000000 / HANDSHK_TIMEOUT = 690
 */
#define QTIMEOUT_200SEC 690

#define QMSPI_ACTIVATE 0x01
#define QMSPI_RESET 0x02
#define QMSPI_TRANSFER_LEN_IN_BYTES 0x400
#define QMSPI_CLOSE_XFER_EN 0x200
#define QMSPI_TX_EN 0x04
#define QMSPI_RX_EN 0x40
#define QMSPI_START 0x01

#define QMSPI_TRANSFER_COMPLETE 0x01
#define QMSPI_DESCR_BUFF_EN 0x10000
#define QMSPI_DESCR_BUFF1 0x1000
#define QMSPI_DESCR_BUFF_LAST 0x10000
#define QMSPI_DESCR_LAST 0x10000
#define QMSPI_TX_EN_0MODE 0x08
#define QMSPI_DUMY_COMMAND 0xDD
#define QMSPI_CLR_DATA_BUFF 0x04
#define QMSPI_TX_DMA_4BYTE 0x30
#define QMSPI_RX_DMA_4BYTE 0x180
#define DMA_XFER_4BYTE 0x4
#define STAT_BUSY_BIT (1 << 0)

#define CLK_DIV 1

/* 4K byte buffer from 0xCA400 - 0xCB400 */
static uint8_t read_data_ptr[4 * 1024] __attribute__((section(".buffer_4K")));

static enum failure_resp_type qmspi_poll_for_status(uint32_t status_val)
{
	uint32_t cnt = HANDSHAKE_TIMEOUT_LONG;
	uint32_t result = 0;

	while (cnt--) {
		result = QMSPI_INST->QMSPI_STATUS;
		if (result & status_val) {
			QMSPI_INST->QMSPI_STATUS = status_val;
			return NO_FAILURE;
		}
	}

	return SPI_OPERATION_FAILURE;
}

static void qmspi_clear_status(void)
{
	QMSPI_INST->QMSPI_MODE = QMSPI_RESET;
	/* Clear status (including TRANSFER_COMPLETE bit 0 ) */
	QMSPI_INST->QMSPI_STATUS = (uint16_t)0xFFFF;
	/* Clear Tx/Rx FIFO buffers */
	QMSPI_INST->QMSPI_EXECUTE = QMSPI_CLR_DATA_BUFF;
	/* Internal SPI flash */
	QMSPI_INST->QMSPI_MODE = CLK_DIV << 16 | QMSPI_ACTIVATE;
}

static enum failure_resp_type qmspi_reset(void)
{
	/* Issue reset enable and SPI reset command */
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;
	enum failure_resp_type ret;

	qmspi_clear_status();
	/* Set WREN as required by flash device */
	QMSPI_INST->QMSPI_CTRL = (1 << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
				 QMSPI_CLOSE_XFER_EN | QMSPI_TX_EN;
	*TX_FIFO = (uint8_t)RSTEN;

	QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

	ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
	if (ret != NO_FAILURE) {
		return ret;
	}

	qmspi_clear_status();

	/* Set WREN as required by flash device */
	QMSPI_INST->QMSPI_CTRL = (1 << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
				 QMSPI_CLOSE_XFER_EN | QMSPI_TX_EN;
	*TX_FIFO = (uint8_t)SPI_RST;

	QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

	ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
	if (ret != NO_FAILURE) {
		return ret;
	}

	qmspi_clear_status();

	return NO_FAILURE;
}

static void qmspi_init(void)
{
	QMSPI_INST->QMSPI_MODE = QMSPI_RESET;
	qmspi_clear_status();
}

static void dma0_reset(void)
{
	DMA_MAIN_INST->DMA_MAIN_CONTROL_b.SOFT_RESET = 1;
	DMA_MAIN_INST->DMA_MAIN_CONTROL = 0;
	DMA_CHAN00_INST->INT_STATUS = 0x07;
}

static void init_signals(uint32_t spi_util_cmd)
{
	EC_REG_BANK_INST->GPIO_BANK_PWR &=
		~(1 << EC_REG_BANK_INST_GPIO_BANK_PWR_VTR_LVL2_Pos);

	/* INT_SPI_MOSI */
	gpio_pin_ctrl1_reg_write(074, 0x1000);
	/* INT_SPI_MISO */
	gpio_pin_ctrl1_reg_write(075, 0x1000);
	/* INT_SPI_nCS */
	gpio_pin_ctrl1_reg_write(0116, 0x1000);
	/* INT_SPI_SCLK */
	gpio_pin_ctrl1_reg_write(0117, 0x1000);
	/* INT_SPI_WP */
	gpio_pin_ctrl1_reg_write(076, 0x1000);
}

static enum failure_resp_type qmspi_read_status(void)
{
	enum failure_resp_type ret;
	uint8_t data = 0;
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;
	volatile uint8_t *RX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_RECEIVE_BUFFER;

	/* Internal SPI flash */
	QMSPI_INST->QMSPI_MODE = CLK_DIV << 16 | QMSPI_ACTIVATE;

	QMSPI_INST->QMSPI_CTRL = QMSPI_DESCR_BUFF_EN;
	QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_0 = (1 << 17) | QMSPI_DESCR_BUFF1 |
						 QMSPI_TRANSFER_LEN_IN_BYTES |
						 QMSPI_TX_EN;
	QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_1 =
		(1 << 17) | QMSPI_DESCR_LAST | QMSPI_TRANSFER_LEN_IN_BYTES |
		QMSPI_CLOSE_XFER_EN | QMSPI_TX_EN_0MODE | QMSPI_RX_EN;
	*TX_FIFO = (uint8_t)READ_STATUS;
	QMSPI_INST->QMSPI_BUFFER_COUNT_TRIGGER = (1 << 16);

	QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

	ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
	data = *RX_FIFO;

	return ret;
}

void spi_flash_init(uint32_t spi_util_cmd)
{
	init_signals(spi_util_cmd);
	qmspi_init();
	dma0_reset();
	qmspi_reset();
	qmspi_read_status();
}

static enum failure_resp_type qmspi_wait_for_not_busy(uint32_t extended_timeout)
{
	uint32_t cnt = HANDSHAKE_TIMEOUT_LONG * extended_timeout;
	uint8_t data = 0;
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;
	volatile uint8_t *RX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_RECEIVE_BUFFER;
	enum failure_resp_type ret = SPI_OPERATION_FAILURE;

	while (cnt--) {
		/* Internal SPI flash */
		QMSPI_INST->QMSPI_MODE = CLK_DIV << 16 | QMSPI_ACTIVATE;
		QMSPI_INST->QMSPI_CTRL = QMSPI_DESCR_BUFF_EN;
		QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_0 =
			(1 << 17) | QMSPI_DESCR_BUFF1 |
			QMSPI_TRANSFER_LEN_IN_BYTES | QMSPI_TX_EN;
		QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_1 =
			(1 << 17) | QMSPI_DESCR_LAST |
			QMSPI_TRANSFER_LEN_IN_BYTES | QMSPI_CLOSE_XFER_EN |
			QMSPI_TX_EN_0MODE | QMSPI_RX_EN;
		*TX_FIFO = (uint8_t)READ_STATUS;
		QMSPI_INST->QMSPI_BUFFER_COUNT_TRIGGER = (1 << 16);

		QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

		qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
		data = *RX_FIFO;

		if ((data & STAT_BUSY_BIT) == 0) {
			ret = NO_FAILURE;
			break;
		}
	}

	return ret;
}

static enum failure_resp_type write_enable(void)
{
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;
	volatile uint8_t *RX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_RECEIVE_BUFFER;
	enum failure_resp_type ret = NO_FAILURE;
	uint8_t done = 0;
	uint8_t data, first_time = 0;
	uint32_t cnt = HANDSHAKE_TIMEOUT;

	qmspi_clear_status();
	ret = qmspi_wait_for_not_busy(TIMEOUT_870MS);
	if (ret != NO_FAILURE) {
		return ret;
	}

	/* Internal SPI flash */
	QMSPI_INST->QMSPI_MODE = CLK_DIV << 16 | QMSPI_ACTIVATE;

	while (!done) {
		QMSPI_INST->QMSPI_CTRL = (1 << 17) |
					 QMSPI_TRANSFER_LEN_IN_BYTES |
					 QMSPI_CLOSE_XFER_EN | QMSPI_TX_EN;
		*TX_FIFO = (uint8_t)WREN_CMD;

		QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;
		ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
		if (ret != NO_FAILURE) {
			return ret;
		}

		cnt = HANDSHAKE_TIMEOUT;
		while (cnt--) {
			__NOP();
		}

		if (first_time == 0) {
			first_time++;
			QMSPI_INST->QMSPI_CTRL =
				(1 << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
				QMSPI_CLOSE_XFER_EN | QMSPI_TX_EN;
			*TX_FIFO = (uint8_t)GLOBAL_UNLOCK_CMD;

			QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;
			ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
			if (ret != NO_FAILURE) {
				return ret;
			}

			cnt = HANDSHAKE_TIMEOUT;
			while (cnt--) {
				__NOP();
			}
		}

		QMSPI_INST->QMSPI_CTRL = QMSPI_DESCR_BUFF_EN;
		QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_0 =
			(1 << 17) | QMSPI_DESCR_BUFF1 |
			QMSPI_TRANSFER_LEN_IN_BYTES | QMSPI_TX_EN;
		QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_1 =
			(1 << 17) | QMSPI_DESCR_LAST |
			QMSPI_TRANSFER_LEN_IN_BYTES | QMSPI_CLOSE_XFER_EN |
			QMSPI_TX_EN_0MODE | QMSPI_RX_EN;
		*TX_FIFO = (uint8_t)READ_STATUS;
		QMSPI_INST->QMSPI_BUFFER_COUNT_TRIGGER = (1 << 16);

		QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

		ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
		data = *RX_FIFO;

		if (data & WEL_BIT) {
			done = 1;
		}
	}

	return ret;
}

static enum failure_resp_type
qmspi_dma_write(uint32_t flash_addr, uint8_t *data_src, uint32_t length)
{
	/*
	 * Program a sector(256 bytes) of flash device starting at address
	 * provided
	 */
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;
	enum failure_resp_type ret;
	uint32_t dma_done = 0;

	qmspi_clear_status();
	write_enable();
	qmspi_clear_status();

	QMSPI_INST->QMSPI_CTRL = QMSPI_DESCR_BUFF_EN;

	QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_0 =
		(4 << 17) | QMSPI_TRANSFER_LEN_IN_BYTES | QMSPI_DESCR_BUFF1 |
		QMSPI_TX_EN;
	*TX_FIFO = PAGE_PROGRAM;
	*TX_FIFO = (flash_addr >> 16) & 0xFF;
	*TX_FIFO = (flash_addr >> 8) & 0xFF;
	*TX_FIFO = flash_addr & 0xFF;

	QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_1 =
		(length << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
		QMSPI_DESCR_BUFF_LAST | QMSPI_CLOSE_XFER_EN |
		QMSPI_TX_DMA_4BYTE | QMSPI_TX_EN;

	DMA_MAIN_INST->DMA_MAIN_CONTROL = 0x02; /* Reset */
	DMA_MAIN_INST->DMA_MAIN_CONTROL = 0x01;
	DMA_CHAN00_INST->DMA_CHANNEL_ACTIVATE = 0x01;
	DMA_CHAN00_INST->CONTROL = 0x00011500 | (DMA_XFER_4BYTE << 20);
	DMA_CHAN00_INST->DEVICE_ADDRESS = QMSPI_INST_BASE + 0x20;
	DMA_CHAN00_INST->MEMORY_START_ADDRESS = (uint32_t)data_src;
	DMA_CHAN00_INST->MEMORY_END_ADDRESS = (uint32_t)(data_src) + length;

	DMA_CHAN00_INST->CONTROL = 0x00011501 | (DMA_XFER_4BYTE << 20);
	QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

	while (1) {
		dma_done = DMA_CHAN00_INST->CONTROL;
		if (dma_done & 0x00000004) {
			break;
		}
	}

	ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
	if (ret != NO_FAILURE) {
		return ret;
	}
	ret = qmspi_wait_for_not_busy(TIMEOUT_870MS);

	DMA_CHAN00_INST->CONTROL = 0;
	return ret;
}

enum failure_resp_type spi_flash_program_sector(uint32_t sector_address,
						uint8_t *input_data_ptr)
{
	uint8_t *dwn_loaded_data_ptr;
	uint32_t page_address = 0;
	uint32_t ret;

	/* program 4K sector worth of data in 256 byte chunks (16*256=4096) */
	for (page_address = 0; page_address < SECTOR_SIZE;) {
		dwn_loaded_data_ptr =
			(uint8_t *)(input_data_ptr + page_address);

		ret = qmspi_dma_write(sector_address + page_address,
				      dwn_loaded_data_ptr, PAGE_SIZE);
		if (ret != NO_FAILURE) {
			return SPI_OPERATION_FAILURE;
		}

		page_address += PAGE_SIZE;
	}

	return NO_FAILURE;
}

static enum failure_resp_type qmspi_dma_read(uint32_t addr, uint32_t length,
					     uint8_t *read_buff)
{
	/* Read flash device starting at address provided */
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;
	enum failure_resp_type ret;
	uint32_t dma_done = 0;
	uint8_t dumy_cnt = 0;
	uint8_t cmd_cnt = 4;

	qmspi_clear_status();

	ret = qmspi_wait_for_not_busy(TIMEOUT_870MS);
	if (ret != NO_FAILURE) {
		return ret;
	}

	QMSPI_INST->QMSPI_CTRL = QMSPI_DESCR_BUFF_EN;

	dumy_cnt = 1;

	QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_0 =
		((cmd_cnt + dumy_cnt) << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
		QMSPI_DESCR_BUFF1 | QMSPI_TX_EN;

	*TX_FIFO = FAST_READ;

	*TX_FIFO = (addr >> 16) & 0xFF;
	*TX_FIFO = (addr >> 8) & 0xFF;
	*TX_FIFO = addr & 0xFF;
	if (dumy_cnt == 1) {
		*TX_FIFO = QMSPI_DUMY_COMMAND;
	}

	QMSPI_INST->QMSPI_DESCRIPTION_BUFFER_1 =
		(length << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
		QMSPI_DESCR_BUFF_LAST | QMSPI_CLOSE_XFER_EN |
		QMSPI_TX_EN_0MODE | QMSPI_RX_EN | QMSPI_RX_DMA_4BYTE;

	QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

	DMA_MAIN_INST->DMA_MAIN_CONTROL = 0x02;
	DMA_MAIN_INST->DMA_MAIN_CONTROL = 0x01;
	DMA_CHAN00_INST->DMA_CHANNEL_ACTIVATE = 0x01;
	DMA_CHAN00_INST->CONTROL = 0x00011600 | (DMA_XFER_4BYTE << 20);
	DMA_CHAN00_INST->DEVICE_ADDRESS = QMSPI_INST_BASE + 0x24;

	DMA_CHAN00_INST->MEMORY_START_ADDRESS = (uint32_t)read_buff;
	DMA_CHAN00_INST->MEMORY_END_ADDRESS = (uint32_t)read_buff + length;
	DMA_CHAN00_INST->CONTROL = 0x00011601 | (DMA_XFER_4BYTE << 20);

	ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
	if (ret != NO_FAILURE) {
		return ret;
	}
	while (1) {
		dma_done = DMA_CHAN00_INST->CONTROL;
		if (dma_done & 0x00000004) {
			break;
		}
	}

	DMA_CHAN00_INST->CONTROL = 0;
	return NO_FAILURE;
}

enum failure_resp_type
spi_splash_check_sector_content_same(uint32_t sector_address, uint8_t *status,
				     uint8_t *input_data_ptr)
{
	uint8_t *dwn_loaded_data_ptr;
	uint32_t ret;
	uint32_t i;

	/* READ a SECTOR (4096Bytes) from device */
	ret = qmspi_dma_read(sector_address, SECTOR_SIZE, read_data_ptr);

	if (ret != NO_FAILURE) {
		return SPI_OPERATION_FAILURE;
	}
	dwn_loaded_data_ptr = (uint8_t *)input_data_ptr;

	*status = 0;

	for (i = 0; i < SECTOR_SIZE; i++) {
		/*
		 * Compare the sector of data we just read from the flash to the
		 * input data to see if there are any differences.
		 */
		if (read_data_ptr[i] != dwn_loaded_data_ptr[i]) {
			*status = 1;
			break;
		}
	}

	return NO_FAILURE;
}

enum failure_resp_type spi_flash_sector_erase(uint32_t addr)
{
	/*
	 * Erase a sector(4k bytes) of flash device starting at address
	 * provided.
	 */
	uint32_t ret = 0;
	volatile uint8_t *TX_FIFO =
		(uint8_t *)&QMSPI_INST->QMSPI_TRANSMIT_BUFFER;

	qmspi_clear_status();
	write_enable();

	QMSPI_INST->QMSPI_STATUS = (uint16_t)0xFFFF;
	QMSPI_INST->QMSPI_EXECUTE = QMSPI_CLR_DATA_BUFF;

	QMSPI_INST->QMSPI_CTRL = (4 << 17) | QMSPI_TRANSFER_LEN_IN_BYTES |
				 QMSPI_CLOSE_XFER_EN | QMSPI_TX_EN;
	*TX_FIFO = (uint8_t)ERASE_SECTOR;
	*TX_FIFO = (uint8_t)((addr >> 16) & 0xFF);
	*TX_FIFO = (uint8_t)((addr >> 8) & 0xFF);
	*TX_FIFO = (uint8_t)(addr & 0xFF);

	QMSPI_INST->QMSPI_EXECUTE = QMSPI_START;

	ret = qmspi_poll_for_status(QMSPI_TRANSFER_COMPLETE);
	if (ret != NO_FAILURE) {
		return SPI_OPERATION_FAILURE;
	}
	qmspi_clear_status();

	/*
	 * Flash device can take up to 200 sec for a full chip erase,
	 * adjust the timeout accordingly.
	 */
	ret = qmspi_wait_for_not_busy(QTIMEOUT_200SEC);
	if (ret != NO_FAILURE) {
		return SPI_OPERATION_FAILURE;
	}
	return NO_FAILURE;
}
