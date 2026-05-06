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

#include <math.h>
#include <stdint.h>
/*********************
 *      DEFINES
 *********************/

/* Status Register bits. */
/* Write in progress */
#define SR_WIP (0x01)
/* Write enable latch */
#define SR_WEL (0x02)

#define SLWTMR_CNT_HIT_CHECK (SLWTMR0->INTSTS & SLWTMR_INTSTS_STS)

/* External Flash GPIO pins */
enum {
	EXTR_SPI_CS_PIN = 107,
	EXTR_SPI_MOSI_PIN = 108,
	EXTR_SPI_MISO_PIN = 109,
	EXTR_SPI_CLK_PIN = 111,
	EXTR_SPI_IO3_PIN = 122,
	EXTR_SPI_IO2_PIN = 124,
};

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int8_t config_command(struct spic_command *command, uint8_t cmd,
			     uint32_t addr, enum spic_address_size addr_size,
			     uint8_t dumm_count);
static int32_t flash_write_enable(void);
static int32_t flash_write_disable(void);
static uint8_t flash_read_sr(void);
static int32_t flash_wait_till_ready(void);

/**********************
 *  STATIC VARIABLES
 **********************/
static struct spic_command command_default = {
    .instruction = {
        .bus_width = SPIC_BUS_SINGLE,
        .disabled = 0,
    },
    .address = {
        .bus_width = SPIC_BUS_SINGLE,
        .size = SPIC_ADDR_SIZE_24,
        .disabled = 0,
    },
    .alt = {
        .size = 0,
        .disabled = 1,
    },
    .dumm_count = 0,
    .data = {
        .bus_width = SPIC_BUS_SINGLE,
    },
};

/**********************
 *      MACROS
 **********************/
/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int32_t intr_flash_pin_init(void)
{
	IOPAD->FLASHWP = IOPAD_FLASHWP_INDETEN_Msk;
	IOPAD->FLASHHOLD = IOPAD_FLASHHOLD_INDETEN_Msk;
	IOPAD->FLASHSI = IOPAD_FLASHSI_INDETEN_Msk;
	IOPAD->FLASHSO = IOPAD_FLASHSO_INDETEN_Msk;
	IOPAD->FLASHCS = IOPAD_FLASHCS_INDETEN_Msk;
	IOPAD->FLASHCLK = IOPAD_FLASHCLK_INDETEN_Msk;

	GPIO->GCR[EXTR_SPI_CS_PIN] &= ~GPIO_GCR_MFCTRL_Msk;
	GPIO->GCR[EXTR_SPI_MOSI_PIN] &= ~GPIO_GCR_MFCTRL_Msk;
	GPIO->GCR[EXTR_SPI_MISO_PIN] &= ~GPIO_GCR_MFCTRL_Msk;
	GPIO->GCR[EXTR_SPI_CLK_PIN] &= ~GPIO_GCR_MFCTRL_Msk;
	GPIO->GCR[EXTR_SPI_IO3_PIN] &= ~GPIO_GCR_MFCTRL_Msk;
	GPIO->GCR[EXTR_SPI_IO2_PIN] &= ~GPIO_GCR_MFCTRL_Msk;

	return 0;
}

int32_t flash_erase_sector(uint32_t address, enum flash_addressing_mode mode)
{
	struct spic_command *command = &command_default;
	enum spic_address_size addr_size = SPIC_ADDR_SIZE_24;
	enum spic_status status;

	uint8_t erase_cmd = FLASH_CMD_SE;

	int32_t rc = 0;
	uint32_t len = 0;

	if (mode == FLASH_ADDRESSING_4BYTE) {
		addr_size = SPIC_ADDR_SIZE_32;
		erase_cmd = FLASH_CMD_SE_4B;
	}

	flash_write_enable();

	config_command(command, erase_cmd, address, addr_size, 0);
	status = spic_write(command, NULL, &len);
	if (status != SPIC_STATUS_OKAY) {
		rc = -1;
		goto err_exit;
	}

	rc = flash_wait_till_ready();

err_exit:
	flash_write_disable();
	return rc;
}

int32_t flash_write_status_reg(uint8_t sr1, uint8_t sr2)
{
	struct spic_command *command = &command_default;
	uint32_t len = 1;
	int32_t rc = 0;

	flash_write_enable();
	config_command(command, FLASH_CMD_WRSR, 0, 0, 0);
	spic_write(command, &sr1, &len);
	rc = flash_wait_till_ready();
	flash_write_disable();

	if (rc != 0) {
		return rc;
	}

	flash_write_enable();
	config_command(command, FLASH_CMD_WRSR2, 0, 0, 0);
	spic_write(command, &sr2, &len);
	rc = flash_wait_till_ready();
	flash_write_disable();

	return rc;
}

int32_t flash_read(uint8_t rdcmd, uint32_t address, uint8_t *data,
		   uint32_t size, enum flash_addressing_mode mode)
{
	struct spic_command *command = &command_default;
	enum spic_address_size addr_size = SPIC_ADDR_SIZE_24;
	enum spic_status status;

	uint32_t src_addr = address;
	uint8_t *dst_idx = data;

	int32_t remind_size = size;
	uint32_t block_size = 0x8000ul;

	if (mode == FLASH_ADDRESSING_4BYTE) {
		addr_size = SPIC_ADDR_SIZE_32;
	}

	while (remind_size > 0) {
		switch (rdcmd) {
		default:
		case 0x03:
			config_command(command, FLASH_CMD_READ, src_addr,
				       addr_size, 0);
			break;
		case 0x0B:
			config_command(command, FLASH_CMD_FREAD, src_addr,
				       addr_size, 8);
			break;
		case 0x3B:
			config_command(command, FLASH_CMD_DREAD, src_addr,
				       addr_size, 8);
			break;
		case 0x6B:
			config_command(command, FLASH_CMD_QREAD, src_addr,
				       addr_size, 8);
			break;
		}

		if (remind_size >= block_size) {
			status = spic_read(command, dst_idx,
					   (uint32_t *)&block_size);
			src_addr += block_size;
			remind_size -= block_size;
		} else {
			status = spic_read(command, dst_idx,
					   (uint32_t *)&remind_size);
			remind_size = 0;
		}

		if (status != SPIC_STATUS_OKAY) {
			return -1;
		}

		dst_idx += block_size;
	}

	return 0;
}

int32_t flash_program_page(uint32_t address, const uint8_t *data, uint32_t size,
			   enum flash_addressing_mode mode)
{
	struct spic_command *command = &command_default;
	enum spic_address_size addr_size = SPIC_ADDR_SIZE_24;
	enum spic_status status;

	uint8_t wr_cmd = FLASH_CMD_PP;

	uint32_t offset = 0, chunk = 0, page_size = FLASH_PAGE_PROGRAM_SIZE;
	int32_t remind_size = size;
	int32_t rc = 0;

	if (mode == FLASH_ADDRESSING_4BYTE) {
		addr_size = SPIC_ADDR_SIZE_32;
		wr_cmd = FLASH_CMD_PP_4B;
	}

	while (remind_size > 0) {
		offset = address % page_size;
		chunk = (offset + remind_size < page_size) ?
				remind_size :
				(page_size - offset);

		flash_write_enable();

		config_command(command, wr_cmd, address, addr_size, 0);
		status = spic_write(command, data, (uint32_t *)&chunk);
		if (status != SPIC_STATUS_OKAY) {
			rc = -1;
			goto err_exit;
		}

		data += chunk;
		address += chunk;
		remind_size -= chunk;

		rc = flash_wait_till_ready();
	}

err_exit:
	flash_write_disable();
	return rc;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static int8_t config_command(struct spic_command *command, uint8_t cmd,
			     uint32_t addr, enum spic_address_size addr_size,
			     uint8_t dumm_count)
{
	int8_t rc = 0;

	switch (cmd) {
	case FLASH_CMD_WREN:
	case FLASH_CMD_WRDI:
	case FLASH_CMD_WRSR:
	case FLASH_CMD_WRSR2:
	case FLASH_CMD_RDID:
	case FLASH_CMD_RDSR:
	case FLASH_CMD_RDSR2:
	case FLASH_CMD_CE:
	case FLASH_CMD_EN4B:
	case FLASH_CMD_EX4B:
	case FLASH_CMD_EXTNADDR_WREAR:
	case FLASH_CMD_EXTNADDR_RDEAR:
	case FLASH_CMD_EN_RST:
	case FLASH_CMD_RST_DEV:
		command->address.disabled = 1;
		command->data.bus_width = SPIC_BUS_SINGLE;
		break;
	case FLASH_CMD_READ:
	case FLASH_CMD_FREAD:
	case FLASH_CMD_SE:
	case FLASH_CMD_SE_4B:
	case FLASH_CMD_BE:
	case FLASH_CMD_RDSFDP:
	case FLASH_CMD_PP:
		command->address.disabled = 0;
		command->address.bus_width = SPIC_BUS_SINGLE;
		command->data.bus_width = SPIC_BUS_SINGLE;
		break;
	case FLASH_CMD_DREAD:
		command->address.disabled = 0;
		command->address.bus_width = SPIC_BUS_SINGLE;
		command->data.bus_width = SPIC_BUS_DUAL;
		break;
	case FLASH_CMD_QREAD:
		command->address.disabled = 0;
		command->address.bus_width = SPIC_BUS_SINGLE;
		command->data.bus_width = SPIC_BUS_QUAD;
		break;
	case FLASH_CMD_2READ:
		command->address.disabled = 0;
		command->address.bus_width = SPIC_BUS_DUAL;
		command->data.bus_width = SPIC_BUS_DUAL;
		break;
	case FLASH_CMD_4READ:
	case FLASH_CMD_4PP:
		command->address.disabled = 0;
		command->address.bus_width = SPIC_BUS_QUAD;
		command->data.bus_width = SPIC_BUS_QUAD;
		break;
	default:
		rc = -1;
		break;
	}

	command->instruction.value = cmd;
	command->address.size = addr_size;
	command->address.value = addr;
	command->dumm_count = dumm_count;

	return rc;
}

static int32_t flash_write_enable(void)
{
	struct spic_command *command = &command_default;
	enum spic_status status;

	uint32_t len = 0;

	config_command(command, FLASH_CMD_WREN, 0, 0, 0);
	status = spic_write(command, NULL, &len);
	if (status != SPIC_STATUS_OKAY) {
		return (int32_t)status;
	}

	return 0;
}

static int32_t flash_write_disable(void)
{
	struct spic_command *command = &command_default;
	enum spic_status status;

	uint32_t len = 0;

	config_command(command, FLASH_CMD_WRDI, 0, 0, 0);
	status = spic_write(command, NULL, &len);
	if (status != SPIC_STATUS_OKAY) {
		return (int32_t)status;
	}

	return 0;
}

static uint8_t flash_read_sr(void)
{
	struct spic_command *command = &command_default;

	uint32_t len = 1;
	uint8_t sr;

	config_command(command, FLASH_CMD_RDSR, 0, 0, 0);
	spic_read(command, &sr, &len);

	return sr;
}

extern void slowtmr_dealy_us(uint32_t us);
static int32_t flash_wait_till_ready(void)
{
	uint8_t sr;
	int32_t timeout_retry;

	timeout_retry = 0;
	slowtmr_dealy_us(100);
	do {
		sr = flash_read_sr();
		if (SLWTMR_CNT_HIT_CHECK) {
			timeout_retry++;
			slowtmr_dealy_us(100);
		}
		if (timeout_retry >= 100000) {
			return -1;
		}
	} while ((sr & SR_WIP) && (timeout_retry < 100000));

	return 0;
}
