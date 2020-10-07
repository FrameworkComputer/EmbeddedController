/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This file implements the UART console application operations. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "com_port.h"
#include "cmd.h"
#include "main.h"
#include "misc_util.h"
#include "opr.h"

/*----------------------------------------------------------------------------
 * Global Variables
 *---------------------------------------------------------------------------
 */
int port_handle = INVALID_HANDLE_VALUE;

/*----------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */
#define MAX_PORT_NAME_SIZE 32
#define OPR_TIMEOUT 10L		 /* 10  seconds */
#define FLASH_ERASE_TIMEOUT 120L /* 120 seconds */

#define STS_MSG_MIN_SIZE 8
#define STS_MSG_APP_END 0x09

#define MAX_SYNC_TRIALS 3

/*----------------------------------------------------------------------------
 * Global variables
 *---------------------------------------------------------------------------
 */
struct command_node cmd_buf[MAX_CMD_BUF_SIZE];
uint8_t resp_buf[MAX_RESP_BUF_SIZE];

/*---------------------------------------------------------------------------
 * Functions prototypes
 *---------------------------------------------------------------------------
 */
static bool opr_send_cmds(struct command_node *cmd_buf, uint32_t cmd_num);

/*----------------------------------------------------------------------------
 * Function implementation
 *----------------------------------------------------------------------------
 */

/*----------------------------------------------------------------------------
 * Function:	opr_usage
 *
 * Parameters:	none.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Prints the console application operation menu.
 *---------------------------------------------------------------------------
 */
void opr_usage(void)
{
	printf("Operations:\n");
	printf("       %s\t\t- Write To Memory/Flash\n", OPR_WRITE_MEM);
	printf("       %s\t\t- Read From Memory/Flash\n", OPR_READ_MEM);
	printf("       %s\t\t- Execute a non-return code\n", OPR_EXECUTE_EXIT);
	printf("       %s\t\t- Execute a returnable code\n", OPR_EXECUTE_CONT);
}

/*----------------------------------------------------------------------------
 * Function:	opr_close_port
 *
 * Parameters:	none
 * Returns:
 * Side effects:
 * Description:
 *		This routine closes the opened COM port by the application
 *---------------------------------------------------------------------------
 */
bool opr_close_port(void)
{
	return com_port_close(port_handle);
}

/*----------------------------------------------------------------------------
 * Function:        opr_open_port
 *
 * Parameters:	port_name - COM Port name.
 *		port_cfg - COM Port configuration structure.
 * Returns:	1 if successful, 0 in the case of an error.
 * Side effects:
 * Description:
 *		Open a specified ComPort device.
 *---------------------------------------------------------------------------
 */
bool opr_open_port(const char *port_name, struct comport_fields port_cfg)
{
	char *full_port_name;

	if (asprintf(&full_port_name, "/dev/%s", port_name) == -1)
		return false;

	if (port_handle > 0)
		com_port_close(port_handle);

	port_handle = com_port_open((const char *)full_port_name, port_cfg);

	if (port_handle <= 0) {
		display_color_msg(FAIL, "\nERROR: COM Port failed to open.\n");
		DISPLAY_MSG(
			("Please select the right serial port or check if "
			 "other serial\n"));
		DISPLAY_MSG(("communication applications are opened.\n"));
		return false;
	}

	display_color_msg(SUCCESS, "Port %s Opened\n", full_port_name);

	return true;
}

/*----------------------------------------------------------------------------
 * Function:	opr_write_chunk
 *
 * Parameters:
 *      buffer  - Input data buffer.
 *      addr    - Memory address to write to.
 *      size    - Data size to write.
 * Returns: true if successful, false in the case of an error.
 * Side effects:
 * Description:
 *   Write data to RAM, starting from the given  address (addr).
 *   Data size is limited to the max chunk size (256 bytes).
 *---------------------------------------------------------------------------
 */
bool opr_write_chunk(uint8_t *buffer, uint32_t addr, uint32_t size)
{
	struct command_node wr_cmd_buf;

	if (size > MAX_RW_DATA_SIZE) {
		display_color_msg(FAIL,
			"ERROR: Block cannot exceed %d\n", MAX_RW_DATA_SIZE);
	}
	/* Initialize response size */
	wr_cmd_buf.resp_size = 1;
	cmd_create_write(addr, size, buffer,
			wr_cmd_buf.cmd, &wr_cmd_buf.cmd_size);
	return opr_send_cmds(&wr_cmd_buf, 1);
}

/*----------------------------------------------------------------------------
 * Function:	opr_read_chunk
 *
 * Parameters:
 *      buffer  - Data read buffer.
 *      addr    - Memory address to read from.
 *      size    - Data size to read.
 * Returns: true if successful, false in the case of an error.
 * Side effects:
 * Description:
 *   Read data from RAM, starting from the given  address (addr).
 *   Data size is limited to the max chunk size (256 bytes).
 *---------------------------------------------------------------------------
 */
bool opr_read_chunk(uint8_t *buffer, uint32_t addr, uint32_t size)
{
	struct command_node rd_cmd_buf;

	if (size > MAX_RW_DATA_SIZE) {
		display_color_msg(FAIL,
			"ERROR: Block cannot exceed %d\n", MAX_RW_DATA_SIZE);
		return false;
	}

	cmd_create_read(addr, ((uint8_t)size - 1),
					rd_cmd_buf.cmd, &rd_cmd_buf.cmd_size);
	rd_cmd_buf.resp_size = size + 3;
	if (opr_send_cmds(&rd_cmd_buf, 1)) {
		if (resp_buf[0] == (uint8_t)(UFPP_READ_CMD)) {
			memcpy(buffer, &resp_buf[1], size);
			return true;
		}
	}
	return false;
}
/*----------------------------------------------------------------------------
 * Function:	opr_write_mem
 *
 * Parameters:	input	- Input (file-name/console), containing data to write.
 *		addr	- Memory address to write to.
 *		size	- Data size to write.
 * Returns:	none.
 * Side effects:
 * Description:
 *	Write data to memory, starting from a given address.
 *	Memory may be Flash (SPI), DRAM (DDR) or SRAM.
 *	The data is retrieved either from an input file or from a console.
 *	Data size is not limited.
 *	Data is sent in 256 bytes chunks (file mode) or 4 bytes chunks
 *	(console mode).
 *---------------------------------------------------------------------------
 */
void opr_write_mem(uint8_t *buffer, uint32_t addr, uint32_t size)
{
	uint32_t cur_addr = addr;
	uint8_t data_buf[256];
	uint32_t write_size, size_remain;
	uint32_t cmd_idx = 1;
	char seps[] = " ";
	char *token = NULL;
	char *stop_str;
	uint32_t block_size = (console) ? sizeof(uint32_t) : MAX_RW_DATA_SIZE;
	struct command_node wr_cmd_buf;

	/* Initialize response size */
	wr_cmd_buf.resp_size = 1;

	DISPLAY_MSG(("Writing [%d] bytes in [%d] packets\n", size,
		((size + (block_size - 1)) / block_size)));

	/* Read first token from string */
	if (console)
		token = strtok(buffer, seps);

	size_remain = size;
	/* Main write loop */
	while (true) {
		if (console) {
			/*
			 * Invert token to double-word and insert the value to
			 * data buffer
			 */
			(*(uint32_t *)data_buf) =
				strtoul(token, &stop_str, BASE_HEXADECIMAL);

			/* Prepare the next iteration */
			token = strtok(NULL, seps);
		}
		write_size = (size_remain > block_size) ?
					block_size : size_remain;
		if (console) {
			cmd_create_write(cur_addr, write_size, data_buf,
					wr_cmd_buf.cmd, &wr_cmd_buf.cmd_size);
		} else {
			cmd_create_write(cur_addr, write_size, buffer,
					wr_cmd_buf.cmd, &wr_cmd_buf.cmd_size);
			buffer += write_size;
		}
		if (opr_send_cmds(&wr_cmd_buf, 1) != true)
			break;

		cmd_disp_write(resp_buf, write_size, cmd_idx,
			((size + (block_size - 1)) / block_size));
		cur_addr += write_size;
		size_remain -= write_size;
		cmd_idx++;
		if (size_remain == 0)
			break;
	}

	DISPLAY_MSG(("\n"));
}

/*----------------------------------------------------------------------------
 * Function:	opr_read_mem
 *
 * Parameters:	output - Output file name, containing data that was read.
 *		addr   - Memory address to read from.
 *		size   - Data size to read.
 * Returns:	none.
 * Side effects:
 * Description:
 *		Read data from memory, starting from a given address.
 *		Memory may be Flash (SPI), DRAM (DDR) or SRAM.
 *		The data is written into an output file, data size is limited
 *		as specified.
 *		Data is received in 256 bytes chunks.
 *---------------------------------------------------------------------------
 */
void opr_read_mem(char *output, uint32_t addr, uint32_t size)
{
	FILE *output_file_id = NULL;
	uint32_t cur_addr;
	uint32_t bytes_left;
	uint32_t read_size;
	uint32_t cmd_idx = 1;
	struct command_node rd_cmd_buf;

	if (!console) {
		output_file_id = fopen(output, "w+b");

		if (output_file_id == NULL) {
			display_color_msg(FAIL,
				"ERROR: could not open output file [%s]\n",
				output);
			return;
		}
	}

	DISPLAY_MSG(("Reading [%d] bytes in [%d] packets\n", size,
		((size + (MAX_RW_DATA_SIZE - 1)) / MAX_RW_DATA_SIZE)));

	for (cur_addr = addr; cur_addr < (addr + size);
		cur_addr += MAX_RW_DATA_SIZE) {
		bytes_left = (uint32_t)(addr + size - cur_addr);
		read_size = MIN(bytes_left, MAX_RW_DATA_SIZE);

		cmd_create_read(cur_addr, ((uint8_t)read_size - 1),
					rd_cmd_buf.cmd, &rd_cmd_buf.cmd_size);
		rd_cmd_buf.resp_size = read_size + 3;

		if (opr_send_cmds(&rd_cmd_buf, 1) != true)
			break;

		cmd_disp_read(resp_buf, read_size, cmd_idx,
			((size + (MAX_RW_DATA_SIZE - 1)) / MAX_RW_DATA_SIZE));

		if (console)
			cmd_disp_data((resp_buf + 1), read_size);
		else
			fwrite((resp_buf + 1), 1, read_size, output_file_id);

		cmd_idx++;
	}

	DISPLAY_MSG(("\n"));
	if (!console)
		fclose(output_file_id);
}

/*----------------------------------------------------------------------------
 * Function:	opr_execute_exit
 *
 * Parameters:	addr - Start address to execute from.
 * Returns:	none.
 * Side effects:	ROM-Code is not in UART command mode anymore.
 * Description:
 *	Execute code starting from a given address.
 *	Memory address may be in Flash (SPI), DRAM (DDR) or SRAM.
 *	No further communication with thr ROM-Code is expected at this point.
 *---------------------------------------------------------------------------
 */
void opr_execute_exit(uint32_t addr)
{
	uint32_t cmd_num;

	cmd_build_exec_exit(addr, cmd_buf, &cmd_num);
	if (opr_send_cmds(cmd_buf, cmd_num) != true)
		return;

	cmd_disp_exec_exit(resp_buf);
}

/*----------------------------------------------------------------------------
 * Function:	opr_execute_return
 *
 * Parameters:	addr - Start address to execute from.
 * Returns:	true if successful, false in the case of an error.
 * Side effects:
 * Description:
 *  Execute code starting from the given address and then check the result.
 *  The executed code should return with the execution result.
 *---------------------------------------------------------------------------
 */
bool opr_execute_return(uint32_t addr)
{
	uint32_t cmd_num;

	cmd_build_exec_ret(addr, cmd_buf, &cmd_num);
	if (opr_send_cmds(cmd_buf, cmd_num) != true)
		return false;

	/*
	 * Check the response command code is UFPP_FCALL_RSLT_CMD and
	 * the return value from monitor is 0x03. (program finish and verify ok)
	 */
	if (resp_buf[1] != (uint8_t)(UFPP_FCALL_RSLT_CMD)
					|| resp_buf[2] != 0x03)
		return false;
	return true;
}

/*----------------------------------------------------------------------------
 * Function:	opr_check_sync
 *
 * Parameters:
 *		baudrate - baud rate to check
 *
 * Returns:
 * Side effects:
 * Description:
 *	Checks whether the Host and the Core are synchoronized in the
 *	specified baud rate
 *---------------------------------------------------------------------------
 */
enum sync_result opr_check_sync(uint32_t baudrate)
{
	uint32_t cmd_num;
	struct command_node *cur_cmd = cmd_buf;
	uint32_t bytes_read = 0;
	uint32_t i;

	port_cfg.baudrate = baudrate;
	if (!com_config_uart(port_handle, port_cfg))
		return SR_ERROR;

	cmd_build_sync(cmd_buf, &cmd_num);

	if (!com_port_write_bin(port_handle, cur_cmd->cmd, cur_cmd->cmd_size))
		return SR_ERROR;

	/* Allow several SYNC trials */
	for (i = 0; i < MAX_SYNC_TRIALS; i++) {
		bytes_read = com_port_read_bin(port_handle, resp_buf, 1);

		/* Quit if succeeded to read a response */
		if (bytes_read == 1)
			break;
		/* Otherwise give the ROM-Code time to answer */
		sleep(1);
	}

	if (bytes_read == 0)
		/*
		 * Unable to read a response from ROM-Code in a reasonable
		 * time
		 */
		return SR_TIMEOUT;

	if (resp_buf[0] != (uint8_t)(UFPP_D2H_SYNC_CMD))
		/* ROM-Code response is not as expected */
		return SR_WRONG_DATA;

	/* Good response */
	return SR_OK;
}

/*----------------------------------------------------------------------------
 * Function:	opr_scan_baudrate
 *
 * Parameters:	none
 * Returns:
 * Side effects:
 * Description:
 *		Scans the baud rate range by sending sync request to the core
 *		and prints the response
 *---------------------------------------------------------------------------
 */
bool opr_scan_baudrate(void)
{
	uint32_t baud = 0;
	uint32_t step;
	enum sync_result sr;
	bool synched = false;
	bool data_received = false;

	/* Scan with HUGE STEPS */
	/* BR_BIG_STEP is percents */
	step = (BR_LOW_LIMIT * BR_BIG_STEP) / 100;
	for (baud = BR_LOW_LIMIT; baud < BR_HIGH_LIMIT; baud += step) {
		sr = opr_check_sync(baud);
		step = (baud * BR_BIG_STEP) / 100;
		if (sr == SR_OK) {
			printf("SR_OK: Baud rate - %d, resp_buf - 0x%x\n",
				baud, resp_buf[0]);
			synched = true;
			step = (baud * BR_SMALL_STEP) / 100;
		} else if (sr == SR_WRONG_DATA) {
			printf("SR_WRONG_DATA: Baud rate - %d, resp_buf - "
			       "0x%x\n", baud, resp_buf[0]);
			data_received = true;
			step = (baud * BR_MEDIUM_STEP) / 100;
		} else if (sr == SR_TIMEOUT) {
			printf("SR_TIMEOUT: Baud rate - %d, resp_buf - 0x%x\n",
				baud, resp_buf[0]);

			if (synched || data_received)
				break;
		} else if (sr == SR_ERROR) {
			printf("SR_ERROR: Baud rate - %d, resp_buf - 0x%x\n",
				baud, resp_buf[0]);
			if (synched || data_received)
				break;
		} else
			printf("Unknown error code: Baud rate - %d, resp_buf - "
				"0x%x\n", baud, resp_buf[0]);
	}

	return true;
}

/*----------------------------------------------------------------------------
 * Function:	opr_send_cmds
 *
 * Parameters:	cmd_buf - Pointer to a Command Buffer.
 *		cmd_num - Number of commands to send.
 * Returns:	1 if successful, 0 in the case of an error.
 * Side effects:
 * Description:
 *	Send a group of commands through COM port.
 *	A command is sent only after a valid response for the previous command
 *	was received.
 *---------------------------------------------------------------------------
 */
static bool opr_send_cmds(struct command_node *cmd_buf, uint32_t cmd_num)
{
	struct command_node *cur_cmd = cmd_buf;
	uint32_t cmd;
	uint32_t read;
	time_t start;
	double elapsed_time;

	for (cmd = 0; cmd < cmd_num; cmd++, cur_cmd++) {
		if (com_port_write_bin(port_handle, cur_cmd->cmd,
						cur_cmd->cmd_size) == true) {
			time(&start);

			do {
				read = com_port_wait_read(port_handle);
				elapsed_time = difftime(time(NULL), start);
			} while ((read < cur_cmd->resp_size) &&
				 (elapsed_time <= OPR_TIMEOUT));
			com_port_read_bin(port_handle, resp_buf,
							cur_cmd->resp_size);

			if (elapsed_time > OPR_TIMEOUT)
				display_color_msg(FAIL,
					"ERROR: [%d] bytes received for read, "
					"[%d] bytes are expected\n",
					read, cur_cmd->resp_size);
		} else {
			display_color_msg(FAIL,
				"ERROR: Failed to send Command number %d\n",
				cmd);
			return false;
		}
	}

	return true;
}

