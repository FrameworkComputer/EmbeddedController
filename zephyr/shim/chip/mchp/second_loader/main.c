/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MEC1727 SOC spi flash update tool
 */

#include "MCHP_MEC172x.h"
#include "common.h"
#include "crc32.h"
#include "failure_response.h"
#include "gpio.h"
#include "serial.h"
#include "spi_flash.h"

#define EC_ACK_BYTE1 0x3C
#define EC_ACK_BYTE2 0xC3

#define PROCESSOR_CLOCK48MHZ 2

#define SUCCESS_RESPONSE_SIZE 5
#define FAILURE_RESPONSE_SIZE 6

/* Max chunk must be a power of 2 */
#define MAX_CHUNK_SIZE 262144U
#define MAX_CHUNK_BUFF_OFFSET (MAX_CHUNK_SIZE - 1)

#define HEADER_START_POS 0
#define HEADER_UTIL_CMD_POS 1
#define HEADER_FLASH_START_ADDR_POS 2
#define HEADER_FLASH_LEN_POS 3
#define HEADER_TERMIN_POS 58

#define HEADER_FLAG 0x4D434850
#define HEADER_TERMINATOR 0x58454F46

#define PKT_CMD_IDX 0
#define PKT_PAYLOAD_LEN_IDX 1
#define PKT_OFFSET_IDX 2
#define PKT_OFFSET_LEN 3
/* (cmd + length + (3) header offset (LSB rx first) */
#define PKT_HEADER_LEN 5
#define PKT_PAYLOAD_IDX 5
#define HDR_PKT_PAYLOAD_SIZE 0xF0
#define PGM_PKT_PAYLOAD_SIZE 128

#define CRC32_SIZE 4
#define PKT_BUF_MAX_SIZE (PKT_HEADER_LEN + HDR_PKT_PAYLOAD_SIZE + CRC32_SIZE)

#define NUM_CMDS 4
enum sys_state {
	STATE_WAIT_FOR_ACK1 = 1,
	STATE_WAIT_FOR_ACK2,
	STATE_WAIT_FOR_HEADER_CMD,
	STATE_WAIT_FOR_PROGRAM_CMD
};

enum cmd_list {
	CMD_ACK1 = 0x33u,
	CMD_ACK2 = 0xCCu,
	CMD_HDR_FILE = 0x65u,
	CMD_PRG_FILE = 0x67u,
};

struct header_conf {
	uint32_t spi_util_cmd;
	uint32_t flash_start_addr;
	uint32_t flash_data_len_total;
};

/* 256KB buffer from 0xCE000 - 0x10E000 */
static uint8_t data_buffer[256 * 1024] __attribute__((section(".buffer_256K")));

static const uint8_t known_cmds[NUM_CMDS] = { CMD_ACK1, CMD_ACK2, CMD_HDR_FILE,
					      CMD_PRG_FILE };

uint32_t board_init(void)
{
	/*
	 * At this point of time PLL should have locked.
	 * Will verify and proceed.
	 * After POR, PLL become stable after about 3ms.
	 */
	uint32_t cnt = 0x10000;

	PCR_INST->PROC_CLK_CNTRL = PROCESSOR_CLOCK48MHZ;
	VBAT_INST->VBAT_SRC_32K = 0x1;
	PCR_INST->VTR_32K_SRC_b.PLL_REF_SOURCE = 0;

	while (PCR_INST->OSC_ID_b.PLL_LOCK == 0) {
		cnt--;
		if (cnt == 0) {
			return BOARD_INIT_ERR;
		}
	};

	return NO_FAILURE;
}

void make_failure_resp_packet(enum failure_resp_type type, uint8_t command,
			      uint8_t *tx_buff)
{
	uint8_t status = 0;
	uint32_t resp_crc32;

	tx_buff[RESP_CMD_POS] = command | FAILURE_RESP_STATUS_BIT;

	switch (type) {
	case PACKET_CRC_FAILURE:
		status = CRC_FAILURE;
		break;
	case PACKET_PAYLOAD_ILLEGAL_LEN:
		status = ILLEGAL_PAYLOAD_LENGTH;
		break;
	case HEADER_PACKET_ILLEGAL_OFFSET:
		status = ILLEGAL_HEADER_OFFSET;
		break;
	case PGM_PACKET_ILLEGAL_OFFSET:
		status = ILLEGAL_FW_IMAGE_OFFSET;
		break;
	case SPI_OPERATION_FAILURE:
		status = SPI_FLASH_ACCESS_ERROR;
		break;
	case PGM_FLASH_DATA_LEN_INCORRECT:
		status = ILLEGAL_FLASH_LENGTH;
		break;
	case HEADER_PACKET_INVALID:
		status = ILLEGAL_HEADER_FORMAT;
		break;
	default:
		break;
	}

	tx_buff[RESP_FAILURE_STATUS_POS] = status;

	resp_crc32 = crc32_init();
	resp_crc32 = crc32_update(resp_crc32, tx_buff, 2);
	resp_crc32 = crc32_finalize(resp_crc32);

	tx_buff[2] = (uint8_t)(resp_crc32 & 0xFF);
	tx_buff[3] = (uint8_t)((resp_crc32 >> 8) & 0xFF);
	tx_buff[4] = (uint8_t)((resp_crc32 >> 16) & 0xFF);
	tx_buff[5] = (uint8_t)((resp_crc32 >> 24) & 0xFF);
}

void send_response(enum failure_resp_type failure_type, uint8_t command)
{
	uint8_t resp_size = SUCCESS_RESPONSE_SIZE;
	uint8_t i;
	uint8_t tx_buff[FAILURE_RESPONSE_SIZE];
	uint32_t resp_crc32;

	if (failure_type == NO_FAILURE) {
		/* Success Response */
		tx_buff[RESP_CMD_POS] = command;

		resp_crc32 = crc32_init();
		resp_crc32 = crc32_update(resp_crc32, tx_buff, 1);
		resp_crc32 = crc32_finalize(resp_crc32);

		tx_buff[1] = (uint8_t)(resp_crc32 & 0xFF);
		tx_buff[2] = (uint8_t)((resp_crc32 >> 8) & 0xFF);
		tx_buff[3] = (uint8_t)((resp_crc32 >> 16) & 0xFF);
		tx_buff[4] = (uint8_t)((resp_crc32 >> 24) & 0xFF);
	} else if (failure_type < INTERNAL_ERROR_START) {
		make_failure_resp_packet(failure_type, command, tx_buff);
		resp_size = FAILURE_RESPONSE_SIZE;
	} else {
		/* (failure_type >= INTERNAL_ERROR_START)
		 * Nothing more to do.  Host must retry the full seqeunce
		 */
		return;
	}

	for (i = 0; i < resp_size; i++) {
		serial_send_host_char(tx_buff[i]);
	}
}

bool is_valid_cmd(uint8_t cmd)
{
	uint8_t n;
	uint8_t status = false;

	for (n = 0; n < NUM_CMDS; n++) {
		if (cmd == known_cmds[n]) {
			status = true;
		}
	}
	return status;
}

enum failure_resp_type receive_packet(uint8_t *data_buffer, uint8_t *pkt_len)
{
	enum failure_resp_type ret;
	uint8_t len;

	/* Read 4-byte (length + (3) header offset (LSB rx first)) */
	ret = serial_receive_host_bytes(&data_buffer[PKT_PAYLOAD_LEN_IDX],
					(PKT_HEADER_LEN - 1));
	if (ret != NO_FAILURE) {
		return ret;
	}

	/* Parse out the length */
	len = data_buffer[PKT_PAYLOAD_LEN_IDX] + CRC32_SIZE;

	if (len > PKT_BUF_MAX_SIZE) {
		return PACKET_PAYLOAD_ILLEGAL_LEN;
	}

	/* Receive Payload + CRC32*/
	ret = serial_receive_host_bytes(&data_buffer[PKT_PAYLOAD_IDX], len);
	if (ret != NO_FAILURE) {
		return ret;
	}

	*pkt_len = PKT_HEADER_LEN + len;

	return NO_FAILURE;
}

enum failure_resp_type verify_pkt_integrity(uint8_t *data_buffer,
					    uint8_t pkt_len)
{
	enum failure_resp_type crc32_status = NO_FAILURE;
	uint8_t len = pkt_len - CRC32_SIZE;
	uint32_t host_crc32;
	uint32_t command_crc32;

	command_crc32 = crc32_init();
	command_crc32 = crc32_update(command_crc32, data_buffer, len);
	command_crc32 = crc32_finalize(command_crc32);

	host_crc32 = data_buffer[len + 0] << 0;
	host_crc32 |= data_buffer[len + 1] << 8;
	host_crc32 |= data_buffer[len + 2] << 16;
	host_crc32 |= data_buffer[len + 3] << 24;

	if (command_crc32 != host_crc32) {
		crc32_status = PACKET_CRC_FAILURE;
	}

	return crc32_status;
}

enum failure_resp_type extract_program_data(uint8_t *data_buffer,
					    uint32_t offset, uint8_t *len,
					    uint8_t *pkt_buffer)
{
	uint8_t i;
	uint32_t ptr_off_set = 0;
	/* Since we have 256K buffer, wrap around */
	uint32_t offset_256K = offset & MAX_CHUNK_BUFF_OFFSET;

	if (pkt_buffer[PKT_PAYLOAD_LEN_IDX] != PGM_PKT_PAYLOAD_SIZE) {
		return PACKET_PAYLOAD_ILLEGAL_LEN;
	}

	ptr_off_set = pkt_buffer[PKT_OFFSET_IDX + 0] << 0;
	ptr_off_set |= pkt_buffer[PKT_OFFSET_IDX + 1] << 8;
	ptr_off_set |= pkt_buffer[PKT_OFFSET_IDX + 2] << 16;

	if (ptr_off_set != offset) {
		return PGM_PACKET_ILLEGAL_OFFSET;
	}

	*len = pkt_buffer[PKT_PAYLOAD_LEN_IDX];

	for (i = 0; i < (*len); i++) {
		data_buffer[offset_256K + i] = pkt_buffer[PKT_PAYLOAD_IDX + i];
	}

	return NO_FAILURE;
}

enum failure_resp_type verify_program_data(uint8_t *pgm_buff,
					   uint32_t flash_addr,
					   uint32_t progrm_length)
{
	uint8_t status;
	uint32_t sector_address;
	enum failure_resp_type spi_err_flag = NO_FAILURE;
	uint32_t input_data_offset = 0;

	for (sector_address = flash_addr; sector_address < progrm_length;) {
		spi_err_flag = spi_splash_check_sector_content_same(
			sector_address, &status, &pgm_buff[input_data_offset]);
		if (spi_err_flag != NO_FAILURE) {
			break;
		}

		if (status) {
			/* Content not match */
			spi_err_flag = FLASH_DATA_COMPARE_ERROR;
			break;
		}

		sector_address += SECTOR_SIZE;
		input_data_offset += SECTOR_SIZE;
	}
	return spi_err_flag;
}

enum failure_resp_type program_data(uint32_t flash_addr, uint8_t *pgm_buffer,
				    uint32_t progrm_length)
{
	enum failure_resp_type ret;
	uint32_t sector_address;
	uint8_t status;
	uint32_t input_data_offset = 0;

	/*
	 * Read sector content.
	 * Check against content to be programmed.
	 * Perform Erase/Program only if content was different.
	 */
	for (sector_address = flash_addr; sector_address < progrm_length;) {
		ret = spi_splash_check_sector_content_same(
			sector_address, &status,
			&pgm_buffer[input_data_offset]);
		if (ret != NO_FAILURE) {
			return ret;
		}

		if (status) {
			/*
			 * Data read from the device was different (even only
			 * one bit) than the input data.
			 */
			ret = spi_flash_sector_erase(sector_address);
			if (ret != NO_FAILURE) {
				return ret;
			}

			/* Now program this sector in 256 byte pages */
			ret = spi_flash_program_sector(
				sector_address, &pgm_buffer[input_data_offset]);
			if (ret != NO_FAILURE) {
				return ret;
			}
		}

		sector_address += SECTOR_SIZE;
		input_data_offset += SECTOR_SIZE;
	}

	ret = verify_program_data(pgm_buffer, flash_addr, progrm_length);

	return ret;
}

enum failure_resp_type extract_header_info(uint32_t *read_hdr_ptr,
					   struct header_conf *hdr_info)
{
	uint32_t hdr_flag = __builtin_bswap32(read_hdr_ptr[HEADER_START_POS]);
	uint32_t terminator =
		__builtin_bswap32(read_hdr_ptr[HEADER_TERMIN_POS]);

	hdr_info->spi_util_cmd =
		__builtin_bswap32(read_hdr_ptr[HEADER_UTIL_CMD_POS]);
	hdr_info->flash_start_addr =
		__builtin_bswap32(read_hdr_ptr[HEADER_FLASH_START_ADDR_POS]);
	hdr_info->flash_data_len_total =
		__builtin_bswap32(read_hdr_ptr[HEADER_FLASH_LEN_POS]);

	if ((hdr_flag != HEADER_FLAG) || (terminator != HEADER_TERMINATOR)) {
		return HEADER_PACKET_INVALID;
	}

	return NO_FAILURE;
}

void process_rxd_data(uint8_t rx_data)
{
	uint8_t len;
	uint8_t pkt_buff[PKT_BUF_MAX_SIZE];
	static enum sys_state state = STATE_WAIT_FOR_ACK1;
	static struct header_conf hdr_info;
	static uint32_t total_offset;
	uint32_t progrm_length;
	enum failure_resp_type status;

	if (!is_valid_cmd(rx_data)) {
		return;
	}

	switch (state) {
	case STATE_WAIT_FOR_ACK1:
		if (rx_data == CMD_ACK1) {
			state = STATE_WAIT_FOR_ACK2;
		}
		break;

	case STATE_WAIT_FOR_ACK2:
		if (rx_data != CMD_ACK2) {
			state = STATE_WAIT_FOR_ACK1;
			break;
		}

		state = STATE_WAIT_FOR_HEADER_CMD;
		serial_send_host_char(EC_ACK_BYTE1);
		serial_send_host_char(EC_ACK_BYTE2);
		break;

	case STATE_WAIT_FOR_HEADER_CMD:
		if (rx_data != CMD_HDR_FILE) {
			state = STATE_WAIT_FOR_ACK1;
			break;
		}

		pkt_buff[PKT_CMD_IDX] = rx_data;
		status = receive_packet(pkt_buff, &len);
		if (status == SERIAL_RECV_TIMEOUT) {
			/* There is not a defined response to the host on a
			 * timeout. The host must retry the full sequence.
			 */
			state = STATE_WAIT_FOR_ACK1;
			break;
		} else if (status == PACKET_PAYLOAD_ILLEGAL_LEN) {
			state = STATE_WAIT_FOR_ACK1;
			send_response(PACKET_PAYLOAD_ILLEGAL_LEN, rx_data);
			break;
		}

		status = verify_pkt_integrity(pkt_buff, len);
		if (status != NO_FAILURE) {
			/* we have retry, dont change the state */
			send_response(PACKET_CRC_FAILURE, rx_data);
			break;
		}

		if (pkt_buff[PKT_PAYLOAD_LEN_IDX] != HDR_PKT_PAYLOAD_SIZE) {
			state = STATE_WAIT_FOR_ACK1;
			send_response(PACKET_PAYLOAD_ILLEGAL_LEN, rx_data);
			break;
		}

		status = extract_header_info(
			(uint32_t *)&pkt_buff[PKT_PAYLOAD_IDX], &hdr_info);
		if (status != NO_FAILURE) {
			state = STATE_WAIT_FOR_ACK1;
			send_response(status, rx_data);
			break;
		}

		spi_flash_init(hdr_info.spi_util_cmd);

		state = STATE_WAIT_FOR_PROGRAM_CMD;
		send_response(NO_FAILURE, rx_data);

		total_offset = 0;
		break;

	case STATE_WAIT_FOR_PROGRAM_CMD:
		if (rx_data != CMD_PRG_FILE) {
			state = STATE_WAIT_FOR_ACK1;
			break;
		}

		if (!hdr_info.flash_data_len_total) {
			state = STATE_WAIT_FOR_ACK1;
			send_response(PGM_FLASH_DATA_LEN_INCORRECT, rx_data);
			break;
		}

		pkt_buff[PKT_CMD_IDX] = rx_data;
		status = receive_packet(pkt_buff, &len);
		if (status == SERIAL_RECV_TIMEOUT) {
			/* There is not a defined response to the host on a
			 * timeout. The host must retry the full sequence.
			 */
			state = STATE_WAIT_FOR_ACK1;
			break;
		} else if (status == PACKET_PAYLOAD_ILLEGAL_LEN) {
			state = STATE_WAIT_FOR_ACK1;
			send_response(PACKET_PAYLOAD_ILLEGAL_LEN, rx_data);
			break;
		}

		status = verify_pkt_integrity(pkt_buff, len);
		if (status != NO_FAILURE) {
			/* we have retry, dont change the state */
			send_response(PACKET_CRC_FAILURE, rx_data);
			break;
		}

		status = extract_program_data(data_buffer, total_offset, &len,
					      pkt_buff);
		if (status != NO_FAILURE) {
			state = STATE_WAIT_FOR_ACK1;
			send_response(status, rx_data);
			break;
		}
		total_offset += len;

		if (!(total_offset & MAX_CHUNK_BUFF_OFFSET) ||
		    (total_offset == hdr_info.flash_data_len_total)) {
			progrm_length = total_offset;

			status = program_data(hdr_info.flash_start_addr,
					      data_buffer, progrm_length);
			if (status != NO_FAILURE) {
				state = STATE_WAIT_FOR_ACK1;
			} else if (total_offset ==
				   hdr_info.flash_data_len_total) {
				/* FLASH program done */
				state = STATE_WAIT_FOR_ACK1;
			} else {
				hdr_info.flash_start_addr += MAX_CHUNK_SIZE;
			}
		}
		send_response(status, rx_data);
		break;

	default:
		break;
	}
}

/*
 * External HOST program loads the resulting binary of building this
 * project into MEC172x SRAM. Host also loads binary data to be programmed to
 * an external Flash device via the QMSPI interface. Once loaded the HOST
 * sets-up the ARMCore parameters in order to have this program execute.
 */
int main(void)
{
	uint8_t rx_data = 0;
	uint32_t ret = NO_FAILURE;

	ret = board_init();
	if (ret == BOARD_INIT_ERR) {
		while (1)
			; /* Hold control here */
	}

	serial_init(); /* Output thru UART 57600, 8Bit,NP,1SB */
	while (1) {
		if (serial_receive_host_char(&rx_data)) {
			process_rxd_data(rx_data);
		}
	}
}
