/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This file creates the UART Program Protocol API commands. */

#include "cmd.h"
#include "lib_crc.h"
#include "main.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Extracting Byte - 8 bit: MSB, LSB */
#define MSB(u16) ((uint8_t)((uint16_t)(u16) >> 8))
#define LSB(u16) ((uint8_t)(u16))

/*----------------------------------------------------------------------------
 *  SPI Flash commands
 *---------------------------------------------------------------------------
 */

#define SPI_READ_JEDEC_ID_CMD 0x9F
#define SPI_WRITE_ENABLE_CMD 0x06
#define SPI_WRITE_DISABLE_CMD 0x04
#define SPI_READ_STATUS_REG_CMD 0x05
#define SPI_WRITE_STATUS_REG_CMD 0x01
#define SPI_READ_DATA_CMD 0x03
#define SPI_PAGE_PRGM_CMD 0x02
#define SPI_SECTOR_ERASE_CMD 0xD8
#define SPI_BULK_ERASE_CMD 0xC7
#define SPI_READ_PID_CMD 0x90

union cmd_addr {
	uint8_t c_adr[4];
	uint32_t h_adr;
};

/*----------------------------------------------------------------------------
 * Function implementation
 *---------------------------------------------------------------------------
 */

/*----------------------------------------------------------------------------
 * Function:	cmd_create_sync
 *
 * Parameters:	   cmd_info - Pointer to a command buffer.
 *		   cmd_len  - Pointer to command length.
 * Returns:	   none.
 * Side effects:
 * Description:
 *		Create a Host to Device SYNC protocol command.
 *		The total command length is written to 'cmd_len'.
 *---------------------------------------------------------------------------
 */
void cmd_create_sync(uint8_t *cmd_info, uint32_t *cmd_len)
{
	uint32_t len = 0;

	/* Build the command buffer */
	cmd_info[len++] = UFPP_H2D_SYNC_CMD;

	/* Return total command length */
	*cmd_len = len;
}

/*---------------------------------------------------------------------------
 * Function:       cmd_create_write
 *
 * Parameters:	addr    - Memory address to write to.
 *		size    - Size of daya (in bytes) to write to memory.
 *		data_buf - Pointer to data buffer containing raw data to write.
 *		cmd_info - Pointer to a command buffer.
 *		cmd_len  - Pointer to command length.
 * Returns:     none.
 * Side effects:
 * Description:
 *	Create a WRITE protocol command.
 *	The command buffer is enclosed by CRC, calculated on command
 *	information and raw data.
 *	The total command length is written to 'cmd_len'.
 *---------------------------------------------------------------------------
 */
void cmd_create_write(uint32_t addr, uint32_t size, uint8_t *data_buf,
		      uint8_t *cmd_info, uint32_t *cmd_len)
{
	uint32_t i;
	union cmd_addr adr_tr;
	uint16_t crc = 0;
	uint32_t len = 0;

	/* Build the command buffer */
	cmd_info[len++] = UFPP_WRITE_CMD;
	cmd_info[len++] = (uint8_t)(size - 1);

	/* Insert Address */
	adr_tr.h_adr = addr;
	cmd_info[len++] = adr_tr.c_adr[3];
	cmd_info[len++] = adr_tr.c_adr[2];
	cmd_info[len++] = adr_tr.c_adr[1];
	cmd_info[len++] = adr_tr.c_adr[0];

	/* Insert data */
	memcpy(&cmd_info[len], data_buf, size);
	len += size;

	/* Calculate CRC */
	for (i = 0; i < len; i++)
		crc = update_crc(crc, (char)cmd_info[i]);

	/* Insert CRC */
	cmd_info[len++] = MSB(crc);
	cmd_info[len++] = LSB(crc);

	/* Return total command length */
	*cmd_len = len;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_create_read
 *
 * Parameters:	addr    - Memory address to read from.
 *		size    - Size of daya (in bytes) to read from memory.
 *		cmd_info - Pointer to a command buffer.
 *		cmd_len  - Pointer to command length.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Create a READ protocol command.
 *		The command buffer is enclosed by CRC, calculated on command
 *		information and raw data.
 *		The total command length is written to 'cmd_len'.
 *---------------------------------------------------------------------------
 */
void cmd_create_read(uint32_t addr, uint8_t size, uint8_t *cmd_info,
		     uint32_t *cmd_len)
{
	uint32_t i;
	union cmd_addr adr_tr;
	uint16_t crc = 0;
	uint32_t len = 0;

	/* Build the command buffer */
	cmd_info[len++] = UFPP_READ_CMD;
	cmd_info[len++] = (uint8_t)size;

	/* Insert Address */
	adr_tr.h_adr = addr;
	cmd_info[len++] = adr_tr.c_adr[3];
	cmd_info[len++] = adr_tr.c_adr[2];
	cmd_info[len++] = adr_tr.c_adr[1];
	cmd_info[len++] = adr_tr.c_adr[0];

	/* Calculate CRC */
	for (i = 0; i < len; i++)
		crc = update_crc(crc, (char)cmd_info[i]);

	/* Insert CRC */
	cmd_info[len++] = MSB(crc);
	cmd_info[len++] = LSB(crc);

	/* Return total command length */
	*cmd_len = len;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_create_exec
 *
 * Parameters:	addr    - Memory address to execute from.
 *		cmd_info - Pointer to a command buffer.
 *		cmd_len  - Pointer to command length.
 * Returns:     none.
 * Side effects:
 * Description:
 *		Create an FCALL protocol command.
 *		The command buffer is enclosed by CRC, calculated on command
 *		information and raw data.
 *		The total command length is written to 'cmd_len'.
 *---------------------------------------------------------------------------
 */
void cmd_create_exec(uint32_t addr, uint8_t *cmd_info, uint32_t *cmd_len)
{
	uint32_t i;
	union cmd_addr adr_tr;
	uint16_t crc = 0;
	uint32_t len = 0;

	/* Build the command buffer */
	cmd_info[len++] = UFPP_FCALL_CMD;
	cmd_info[len++] = 0;

	/* Insert Address */
	adr_tr.h_adr = addr;
	cmd_info[len++] = adr_tr.c_adr[3];
	cmd_info[len++] = adr_tr.c_adr[2];
	cmd_info[len++] = adr_tr.c_adr[1];
	cmd_info[len++] = adr_tr.c_adr[0];

	/* Calculate CRC */
	for (i = 0; i < len; i++)
		crc = update_crc(crc, (char)cmd_info[i]);

	/* Insert CRC */
	cmd_info[len++] = MSB(crc);
	cmd_info[len++] = LSB(crc);

	/* Return total command length */
	*cmd_len = len;
}

/*---------------------------------------------------------------------------
 * Function:        cmd_build_sync
 *
 * Parameters:	cmd_buf - Pointer to a command buffer.
 *		cmd_num - Pointer to command number.
 * Returns:	none.
 * Description:
 *		Build a synchronization command buffer.
 *		The total command number is written to 'cmd_num'.
 *---------------------------------------------------------------------------
 */
void cmd_build_sync(struct command_node *cmd_buf, uint32_t *cmd_num)
{
	uint32_t cmd = 0;

	cmd_create_sync(cmd_buf[cmd].cmd, &cmd_buf[cmd].cmd_size);
	cmd_buf[cmd].resp_size = 1;
	cmd++;

	*cmd_num = cmd;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_build_exec_exit
 *
 * Parameters:	addr   - Memory address to execute from.
 *		cmd_buf - Pointer to a command buffer.
 *		cmd_num - Pointer to command number.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Build an Excute command buffer.
 *		Command does not expect the executed code to return, that is,
 *		only FCALL protocol
 *		command code is expected.
 *		Determine the expected response size per each command.
 *		The total command number is written to 'cmd_num'.
 *---------------------------------------------------------------------------
 */
void cmd_build_exec_exit(uint32_t addr, struct command_node *cmd_buf,
			 uint32_t *cmd_num)
{
	uint32_t cmd = 0;

	cmd_create_exec(addr, cmd_buf[cmd].cmd, &cmd_buf[cmd].cmd_size);
	cmd_buf[cmd].resp_size = 1;
	cmd++;

	*cmd_num = cmd;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_build_exec_ret
 *
 * Parameters:	addr   - Memory address to execute from.
 *		cmd_buf - Pointer to a command buffer.
 *		cmd_num - Pointer to command number.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Build an Excute command buffer.
 *		Command expects the executed code to return, that is,
 *		FCALL_RSLT protocol command
 *		code is expected, together with the execution result.
 *		Determine the expected response size per each command.
 *		The total command number is written to 'cmd_num'.
 *---------------------------------------------------------------------------
 */
void cmd_build_exec_ret(uint32_t addr, struct command_node *cmd_buf,
			uint32_t *cmd_num)
{
	uint32_t cmd = 0;

	cmd_create_exec(addr, cmd_buf[cmd].cmd, &cmd_buf[cmd].cmd_size);
	cmd_buf[cmd].resp_size = 3;
	cmd++;

	*cmd_num = cmd;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_disp_sync
 *
 * Parameters:	resp_buf - Pointer to a response buffer.
 * Returns:	TRUE if successful, FALSE in the case of an error.
 * Side effects:
 * Description:
 *		Display SYNC command response information.
 *---------------------------------------------------------------------------
 */
bool cmd_disp_sync(uint8_t *resp_buf)
{
	if (resp_buf[0] == (uint8_t)(UFPP_D2H_SYNC_CMD)) {
		display_color_msg(SUCCESS, "Host/Device are synchronized\n");
		return true;
	}

	display_color_msg(FAIL, "Host/Device synchronization failed!!!\n");
	return false;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_disp_write
 *
 * Parameters:	resp_buf - Pointer to a response buffer.
 *		resp_size - Response size.
 *		resp_num - Response packet number.
 * Returns:	TRUE if successful, FALSE in the case of an error.
 * Side effects:
 * Description:
 *		Display WRITE command response information.
 *---------------------------------------------------------------------------
 */
bool cmd_disp_write(uint8_t *resp_buf, uint32_t resp_size, uint32_t resp_num,
		    uint32_t total_size)
{
	if (resp_buf[0] == (uint8_t)(UFPP_WRITE_CMD)) {
		display_color_msg(
			SUCCESS,
			"\rTransmitted packet of size %u bytes, packet "
			"[%u]out of [%u]",
			resp_size, resp_num, total_size);
		return true;
	}

	display_color_msg(FAIL, "\nWrite packet [%lu] Failed\n", resp_num);
	return false;
}

/*-----------------------------------------------------------------------------
 * Function:	cmd_disp_read
 *
 * Parameters:	resp_buf  - Pointer to a response buffer.
 *		resp_size - Response size.
 *		resp_num  - Response packet number.
 * Returns:	TRUE if successful, FALSE in the case of an error.
 * Side effects:
 * Description:
 *		Display READ command response information.
 *---------------------------------------------------------------------------
 */
bool cmd_disp_read(uint8_t *resp_buf, uint32_t resp_size, uint32_t resp_num,
		   uint32_t total_size)
{
	if (resp_buf[0] == (uint8_t)(UFPP_READ_CMD)) {
		display_color_msg(
			SUCCESS,
			"\rReceived packet of size %u bytes, packet [%u] out "
			"of [%u]",
			resp_size, resp_num, total_size);
		fflush(stdout);
		return true;
	}

	display_color_msg(FAIL, "\nRead packet [%u] Failed\n", resp_num);
	return false;
}

/*----------------------------------------------------------------------------
 * Function:	cmd_disp_data
 *
 * Parameters:	resp_buf  - Pointer to a response buffer.
 *		resp_size - Response size.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Display raw data, read from memory.
 *---------------------------------------------------------------------------
 */
void cmd_disp_data(uint8_t *resp_buf, uint32_t resp_size)
{
	uint32_t idx;
	uint32_t i;

	for (idx = 0; idx < resp_size; idx += 4) {
		if ((idx % 16) == 0)
			printf("\n");

		printf("0x");
		for (i = 4; i > 0; i--)
			printf("%02x", resp_buf[idx + i - 1]);

		if ((idx % 4) == 0)
			printf(" ");
	}

	printf("\n");
}

/*----------------------------------------------------------------------------
 * Function:	cmd_disp_flash_erase_dev
 *
 * Parameters:	resp_buf - Pointer to a response buffer.
 *		dev_num  - Flash Device Number.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Display BULK_ERASE command response information.
 *---------------------------------------------------------------------------
 */
void cmd_disp_flash_erase_dev(uint8_t *resp_buf, uint32_t dev_num)
{
	if (resp_buf[0] == (uint8_t)(UFPP_WRITE_CMD)) {
		display_color_msg(SUCCESS,
				  "Flash Erase of device [%u] Passed\n",
				  dev_num);
	} else {
		display_color_msg(FAIL, "Flash Erase of device [%u] Failed\n",
				  dev_num);
	}
}

/*---------------------------------------------------------------------------
 * Function:	cmd_disp_flash_erase_sect
 *
 * Parameters:	resp_buf - Pointer to a response buffer.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Display BULK_ERASE command response information.
 *---------------------------------------------------------------------------
 */
void cmd_disp_flash_erase_sect(uint8_t *resp_buf, uint32_t dev_num)
{
	if (resp_buf[0] == (uint8_t)(UFPP_WRITE_CMD)) {
		display_color_msg(SUCCESS,
				  "Sector Erase of device [%lu] Passed\n",
				  dev_num);
	} else {
		display_color_msg(FAIL, "Sector Erase of device [%lu] Failed\n",
				  dev_num);
	}
}

/*---------------------------------------------------------------------------
 * Function:	cmd_disp_exec_exit
 *
 * Parameters:	resp_buf - Pointer to a response buffer.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Display Execute command response information.
 *---------------------------------------------------------------------------
 */
void cmd_disp_exec_exit(uint8_t *resp_buf)
{
	if (resp_buf[0] == (uint8_t)(UFPP_FCALL_CMD))
		display_color_msg(SUCCESS, "Execute Command Passed\n");
	else
		display_color_msg(FAIL, "Execute Command Failed\n");
}

/*---------------------------------------------------------------------------
 * Function:	cmd_disp_exec_ret
 *
 * Parameters:	resp_buf - Pointer to a response buffer.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Display Execute Result command response information.
 *---------------------------------------------------------------------------
 */
void cmd_disp_exec_ret(uint8_t *resp_buf)
{
	if (resp_buf[1] == (uint8_t)(UFPP_FCALL_RSLT_CMD)) {
		display_color_msg(
			SUCCESS,
			"Execute Command Passed, execution result is [0x%X]\n",
			resp_buf[2]);
	} else {
		display_color_msg(
			FAIL,
			"Execute Command Failed  [0x%X]  [0x%X], rslt=[0x%X]\n",
			resp_buf[0], resp_buf[1], resp_buf[2]);
	}
}
