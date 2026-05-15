/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * For Chipset: RTK EC
 *
 * Function: RTK Flash Utility
 */

/*********************
 *      INCLUDES
 *********************/

#include "flash_map_backend.h"
#include "reg.h"

#include <stdint.h>
#include <stdio.h>
/*********************
 *      DEFINES
 *********************/

#define RTS_MONITOR_UUT_TAG 0x4352544Bul
#define RTS_MONITOR_HEADER_ADDR 0x20010000ul
#define RTS_TEMP_DATA_ADDR 0x20020000ul
#define RTS_CMD_SEL_ADDR 0x2005F000ul
#define RTS_CMD_FLASH_READ 0xA5A5A5A5ul
#define RTS_CMD_ERASE_ONLY 0x5A5A5A5Aul
#define RTS_CMD_PROBE_CAP_OFFSET 0x50524F42ul
#define RTS_CMD_PROBE_CAP_SIZE 0x00000000ul
#define RTS_SPI_PROGRAMMING_FLAG 0x20018000ul

#define WRITE_TO_FLASH_COMPLETE (1ul << 0)
#define WRITE_TO_FLASH_VERIFIED (1ul << 1)
#define FLASH_READ_COMPLETE (1ul << 2)

#define RTK_FLAME_FEATURE_ERASE_ONLY (1ul << 0)
#ifndef RTK_FLAME_VERSION_STR
#define RTK_FLAME_VERSION_STR "unknown"
#endif
#define RTK_FLAME_METADATA_MAGIC 0x464C414Dul

struct rtk_flame_metadata {
	uint32_t magic;
	char version[32];
	uint32_t features;
};

const struct rtk_flame_metadata monitor_metadata
	__attribute__((section(".rodata.metadata"))) = {
		.magic = RTK_FLAME_METADATA_MAGIC,
		.version = RTK_FLAME_VERSION_STR,
		.features = RTK_FLAME_FEATURE_ERASE_ONLY,
	};

#define WRITE_TO_FLASH_ERASE_ERROR (0x10)
#define WRITE_TO_FLASH_WRITE_ERROR (0x20)
#define WRITE_TO_FLASH_NOT_ERASED_ERROR (0x30)
#define WRITE_TO_FLASH_STATUS_REG_ERROR (0x40)

/**********************
 *      TYPEDEFS
 **********************/

struct monitor_header_tag {
	/* offset 0x00: TAG NPCX_MONITOR_TAG */
	uint32_t tag;
	/* offset 0x04: Size of the binary being programmed (in bytes) */
	uint32_t size;
	/* offset 0x08: The RAM address of the binary to program into the SPI */
	uint32_t src_addr;
	/* offset 0x0C: The Flash address to be programmed (Absolute address) */
	uint32_t dest_addr;
	/* offset 0x10: Maximum allowable flash clock frequency */
	uint8_t max_clock;
	/* offset 0x11: SPI Flash read mode */
	uint8_t read_mode;
	/* offset 0x12: Reserved */
	uint16_t reserved;
} __packed;

/**********************
 *  EXTERN PROTOTYPES
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int32_t write_to_flash(uint32_t *flag_upload, int spi_offset,
			      uint32_t sz_image, const uint8_t *image_base);
static int32_t eflash_erase(int offset, int size);
static int32_t eflash_write(int offset, int size, const uint8_t *data);
static int eflash_verify(int offset, int size, const uint8_t *data);
static void eflash_read(int offset, int size, uint8_t *data);
static void uart_init_pll_115200(void);
static void serial_polling_send(const char *buf, uint32_t len);
static void slowtmr_timeout_reach(uint8_t error_code);
/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/
#define SLWTMR_CNT_HIT_CHECK (SLWTMR0->INTSTS & SLWTMR_INTSTS_STS)
#define UART_CLK_SRC_CHECK (SYSTEM->UARTCLK & SYSTEM_UARTCLK_SRC_Msk)
#define GPIO_UART_FUNCTION_CHECK                   \
	(!((GPIO->GCR[113] & 0x0700) == 0x0100) || \
	 !((GPIO->GCR[114] & 0x0700) == 0x0100))
/**********************
 *   GLOBAL FUNCTIONS
 **********************/

int spic_flash_upload(void)
{
	uint32_t *flag_upload;
	flag_upload = (uint32_t *)(RTS_SPI_PROGRAMMING_FLAG);
	*flag_upload = 0;
	int32_t ret;

	SYSTEM->PERICLKPWR1 |= SYSTEM_PERICLKPWR1_SLWTMR0CLKPWR_Msk;
	slowtmr_dealy_us(1000);
	while ((UART->LSR & UART_LSR_THRE_Msk) == 0) {
		if (SLWTMR_CNT_HIT_CHECK) {
			UART->THR = 0x10;
			break;
		}
	}
	if (SLWTMR_CNT_HIT_CHECK) {
		slowtmr_timeout_reach(0x11);
		return *flag_upload;
	}
	slowtmr_dealy_us(1000);
	while (UART->USR & UART_USR_BUSY_Msk) {
		if (SLWTMR_CNT_HIT_CHECK) {
			UART->THR = 0x20;
			break;
		}
	}
	if (SLWTMR_CNT_HIT_CHECK) {
		slowtmr_timeout_reach(0x22);
		return *flag_upload;
	}
	slowtmr_dealy_us(1000);
	while (UART->TFL) {
		if (SLWTMR_CNT_HIT_CHECK) {
			UART->THR = 0x30;
			break;
		}
	}
	if (SLWTMR_CNT_HIT_CHECK) {
		slowtmr_timeout_reach(0x33);
		return *flag_upload;
	}

	/*
	 * Flash image has been uploaded to Code RAM
	 */
	uint32_t sz_image;
	uint32_t uut_tag;
	const uint8_t *image_base;
	struct monitor_header_tag *monitor_header =
		(struct monitor_header_tag *)(RTS_MONITOR_HEADER_ADDR);
	int spi_offset;

	uut_tag = monitor_header->tag;
	/* If it is UUT tag, read required parameters from header */
	if (uut_tag == RTS_MONITOR_UUT_TAG) {
		sz_image = monitor_header->size;
		spi_offset = monitor_header->dest_addr;
		image_base = (const uint8_t *)(monitor_header->src_addr);
	} else {
		*flag_upload = 0x08;
		if (GPIO_UART_FUNCTION_CHECK) {
			GPIO->GCR[113] &= ~(0x00000700);
			GPIO->GCR[114] &= ~(0x00000700);
			GPIO->GCR[113] |= 0x0100;
			GPIO->GCR[114] |= 0x0100;
		}
		return -1;
	}

	intr_flash_pin_init();
	spic_init(3, 0);

	if (UART_CLK_SRC_CHECK == 0) {
		uart_init_pll_115200();
	}

	UART->FCR |= 0x47;

	uint32_t *temp_to_load;
	temp_to_load = (uint32_t *)RTS_CMD_SEL_ADDR;
	if (spi_offset == RTS_CMD_PROBE_CAP_OFFSET &&
	    sz_image == RTS_CMD_PROBE_CAP_SIZE) {
		*flag_upload |= WRITE_TO_FLASH_VERIFIED;
		serial_polling_send((const char *)&monitor_metadata,
				    sizeof(monitor_metadata));
	} else if (*temp_to_load == RTS_CMD_FLASH_READ) {
		uint8_t *read_buf = (uint8_t *)image_base;
		eflash_read(spi_offset, sz_image, read_buf);
		*flag_upload |= FLASH_READ_COMPLETE;
	} else if (*temp_to_load == RTS_CMD_ERASE_ONLY) {
		ret = write_to_flash(flag_upload, spi_offset, sz_image, NULL);
	} else {
		ret = write_to_flash(flag_upload, spi_offset, sz_image,
				     image_base);
	}
	/* Mark we have finished upload work */
	*flag_upload |= WRITE_TO_FLASH_COMPLETE;

	/* Return the status back to ROM code is required for UUT */
	if (uut_tag == RTS_MONITOR_UUT_TAG) {
		if (GPIO_UART_FUNCTION_CHECK) {
			GPIO->GCR[113] &= ~(0x00000700);
			GPIO->GCR[114] &= ~(0x00000700);
			GPIO->GCR[113] |= 0x0100;
			GPIO->GCR[114] |= 0x0100;
		}
		return *flag_upload;
	}

	if (GPIO_UART_FUNCTION_CHECK) {
		GPIO->GCR[113] &= ~(0x00000700);
		GPIO->GCR[114] &= ~(0x00000700);
		GPIO->GCR[113] |= 0x0100;
		GPIO->GCR[114] |= 0x0100;
	}

	/* Infinite loop */
	for (;;)
		;

	return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static int32_t write_to_flash(uint32_t *flag_upload, int spi_offset,
			      uint32_t sz_image, const uint8_t *image_base)
{
	int32_t ret = 0;

	/* Clear both status registers to ensure write protect is disabled */
	ret = flash_write_status_reg(0, 0);
	if (ret != 0) {
		*flag_upload |= WRITE_TO_FLASH_STATUS_REG_ERROR;
		return ret;
	}

	/* Start to erase */
	ret = eflash_erase(spi_offset, sz_image);
	if (ret != 0) {
		*flag_upload |= WRITE_TO_FLASH_ERASE_ERROR;
		return ret;
	}

	/* Verify flash is erased */
	if (eflash_verify(spi_offset, sz_image, NULL) != 0) {
		*flag_upload |= WRITE_TO_FLASH_NOT_ERASED_ERROR;
		return ret;
	}

	/* Start to write */
	if (image_base != NULL) {
		ret = eflash_write(spi_offset, sz_image, image_base);
		if (ret != 0) {
			*flag_upload |= WRITE_TO_FLASH_WRITE_ERROR;
			return ret;
		}

		/* Verify data */
		if (eflash_verify(spi_offset, sz_image, image_base) == 0) {
			*flag_upload |= WRITE_TO_FLASH_VERIFIED;
			return ret;
		}
	} else {
		/* Erase only complete and verified */
		*flag_upload |= WRITE_TO_FLASH_VERIFIED;
		return ret;
	}

	return ret;
}

static int32_t eflash_erase(int offset, int size)
{
	int32_t ret = 0;
	/* Alignment has been checked in upper layer */
	for (; size > 0; size -= FLASH_SECTOR_EARSE_SIZE,
			 offset += FLASH_SECTOR_EARSE_SIZE) {
		ret = flash_erase_sector(offset, FLASH_ADDRESSING_3BYTE);
		if (ret != 0) {
			return ret;
		}
	}

	return ret;
}

static int32_t eflash_write(int offset, int size, const uint8_t *data)
{
	int dest_addr = offset;
	int32_t ret = 0;
	const int sz_page = FLASH_PAGE_PROGRAM_SIZE;

	/* Write the data per FLASH_PAGE_PROGRAM_SIZE bytes */
	for (; size >= sz_page; size -= sz_page) {
		ret = flash_program_page(dest_addr, (uint8_t *)data, sz_page,
					 FLASH_ADDRESSING_3BYTE);

		if (ret) {
			return ret;
		}

		data += sz_page;
		dest_addr += sz_page;
	}

	/* Handle final partial page, if any */
	if (size != 0) {
		ret = flash_program_page(dest_addr, (uint8_t *)data, size,
					 FLASH_ADDRESSING_3BYTE);
	}

	return ret;
}

static void serial_polling_send(const char *buf, uint32_t len)
{
	if (GPIO_UART_FUNCTION_CHECK) {
		GPIO->GCR[113] &= ~(0x00000700);
		GPIO->GCR[114] &= ~(0x00000700);
		GPIO->GCR[113] |= 0x0100;
		GPIO->GCR[114] |= 0x0100;
	}

	uint32_t i = 0;
	for (i = 0; i < len; i++) {
		while ((UART->LSR & UART_LSR_THRE_Msk) == 0) {
			;
		}
		UART->THR = buf[i];
	}
}

static void eflash_read(int offset, int size, uint8_t *data)
{
	int dest_addr = offset;
	const int sz_page = FLASH_PAGE_PROGRAM_SIZE;

	/* read in FLASH_PAGE_PROGRAM_SIZE bytes */
	for (; size >= sz_page; size -= sz_page) {
		flash_read(0x03, dest_addr, data, sz_page,
			   FLASH_ADDRESSING_3BYTE);

		serial_polling_send(data, sz_page);

		data += sz_page;
		dest_addr += sz_page;
	}

	/* Handle final partial page, if any */
	if (size != 0) {
		flash_read(0x03, dest_addr, data, size, FLASH_ADDRESSING_3BYTE);
		serial_polling_send(data, size);
	}
}

static int eflash_verify(int offset, int size, const uint8_t *data)
{
	int dest_addr = offset;
	const int sz_page = FLASH_PAGE_PROGRAM_SIZE;

	uint32_t i;
	uint8_t tmp;
	uint8_t rd_buf[FLASH_PAGE_PROGRAM_SIZE];

	/* read and verify the data per FLASH_PAGE_PROGRAM_SIZE bytes */
	for (; size >= sz_page; size -= sz_page) {
		flash_read(0x03, dest_addr, rd_buf, sz_page,
			   FLASH_ADDRESSING_3BYTE);
		for (i = 0; i < sz_page; i++) {
			if (data != NULL) {
				tmp = *(data + i);
			} else {
				tmp = 0xff;
			}
			if (rd_buf[i] != tmp) {
				return -1;
			}
		}

		if (data != NULL) {
			data += sz_page;
		}
		dest_addr += sz_page;
	}

	/* Handle final partial page, if any */
	if (size != 0) {
		flash_read(0x03, dest_addr, rd_buf, size,
			   FLASH_ADDRESSING_3BYTE);
		for (i = 0; i < size; i++) {
			if (data != NULL) {
				tmp = *(data + i);
			} else {
				tmp = 0xff;
			}
			if (rd_buf[i] != tmp) {
				return -1;
			}
		}
	}

	return 0;
}

/*
 * uart_init_pll_115200
 */
static void uart_init_pll_115200(void)
{
	uint32_t temp = 0;

	UART->SRR |= UART_SRR_UR_Msk;
	UART->SRR |= UART_SRR_RFR_Msk;
	UART->SRR |= UART_SRR_XFR_Msk;

	SYSTEM->UARTCLK |= SYSTEM_UARTCLK_SRC_Msk;
	SYSTEM->UARTCLK &= ~SYSTEM_UARTCLK_DIV_Msk;

	/*
	 * Set UART parameter
	 *  - 8-bit data, 1-bit stop, no parity
	 */
	UART->LCR |= (3 << UART_LCR_DLS_Pos);
	UART->LCR &= ~UART_LCR_STOP_Msk;
	UART->LCR &= ~UART_LCR_PEN_Msk;

	/* Enable FIFO mode */
	UART->FCR |= UART_FCR_FIFOE_Msk;

	/*
	 * Set baud rate
	 *  - Baud rate = (uart_clk) / (16 * divisor)
	 */
	UART->LCR |= UART_LCR_DLAB_Msk;
	UART->DLH = 0x00000000ul;
	UART->DLL = 0x00000036ul;
	UART->LCR &= ~UART_LCR_DLAB_Msk;

	/* Clear IIR status*/
	temp = UART->USR;
	temp = UART->RBR;
	temp = UART->IIR;
	temp = UART->LSR;
}

/*
 * slowtmr_dealy_us
 */
void slowtmr_dealy_us(uint32_t us)
{
	SLWTMR0->LDCNT = us;

	SLWTMR0->CTRL &= ~SLWTMR_CTRL_EN;
	SLWTMR0->CTRL &= ~SLWTMR_CTRL_MDSELS_PERIOD;
	SLWTMR0->CTRL |= SLWTMR_CTRL_MDSELS_ONESHOT;
	SLWTMR0->INTSTS |= SLWTMR_INTSTS_STS;

	SLWTMR0->CTRL |= SLWTMR_CTRL_EN;
}

/*
 * slowtmr_timeout_reach
 */
static void slowtmr_timeout_reach(uint8_t error_code)
{
	uint32_t *flag_upload;
	flag_upload = (uint32_t *)(RTS_SPI_PROGRAMMING_FLAG);
	*flag_upload = 0;

	SLWTMR0->INTSTS |= SLWTMR_INTSTS_STS;
	SLWTMR0->CTRL &= ~SLWTMR_CTRL_EN;
	*flag_upload = error_code;
	if (GPIO_UART_FUNCTION_CHECK) {
		GPIO->GCR[113] &= ~(0x00000700);
		GPIO->GCR[114] &= ~(0x00000700);
		GPIO->GCR[113] |= 0x0100;
		GPIO->GCR[114] |= 0x0100;
	}
	UART->THR = error_code;
}
