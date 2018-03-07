/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_UUT_CMD_H
#define __UTIL_UUT_CMD_H

#include <stdbool.h>

/*---------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */

#define MAX_CMD_BUF_SIZE 10
#define MAX_RESP_BUF_SIZE 512

enum uart_protocol_cmd {
	UFPP_H2D_SYNC_CMD = 0x55,   /* Single-Byte Host to Device	 */
				    /* synchronization command	 */
	UFPP_D2H_SYNC_CMD = 0x5A,   /* Single-Byte Device to Host	 */
				    /* synchronization response	 */
	UFPP_WRITE_CMD = 0x07,      /* Write command and response	 */
	UFPP_READ_CMD = 0x1C,       /* Read command and response	 */
	UFPP_READ_CRC_CMD = 0x89,   /* Read CRC command and response */
	UFPP_FCALL_CMD = 0x70,      /* Call function command	 */
	UFPP_FCALL_RSLT_CMD = 0x73, /* Call function response	 */
	UFPP_SPI_CMD = 0x92,        /* SPI specific command		 */
	UFPP_ERROR_CMD = 0xFF       /* Error response		 */
};

struct command_node {
	uint8_t cmd[512];
	uint32_t cmd_size;
	uint32_t resp_size;
};

/*---------------------------------------------------------------------------
 * Functions prototypes
 *---------------------------------------------------------------------------
 */

void cmd_create_sync(uint8_t *cmd_info, uint32_t *cmd_len);
void cmd_create_write(uint32_t addr, uint32_t size, uint8_t *data_buf,
					uint8_t *cmd_info, uint32_t *cmd_len);
void cmd_create_read(uint32_t addr, uint8_t size, uint8_t *cmd_info,
					uint32_t *cmd_len);
void cmd_create_exec(uint32_t addr, uint8_t *cmd_info, uint32_t *cmd_len);

void cmd_build_sync(struct command_node *cmd_buf, uint32_t *cmd_num);
void cmd_build_exec_exit(uint32_t addr, struct command_node *cmd_buf,
						uint32_t *cmd_num);
void cmd_build_exec_ret(uint32_t addr, struct command_node *cmd_buf,
						uint32_t *cmd_num);

bool cmd_disp_sync(uint8_t *resp_buf);
bool cmd_disp_write(uint8_t *resp_buf, uint32_t resp_size, uint32_t resp_num,
					uint32_t total_size);
bool cmd_disp_read(uint8_t *resp_buf, uint32_t resp_size, uint32_t resp_num,
					uint32_t total_size);
void cmd_disp_data(uint8_t *resp_buf, uint32_t resp_size);
void cmd_disp_flash_erase_dev(uint8_t *resp_buf, uint32_t dev_num);
void cmd_disp_flash_erase_sect(uint8_t *resp_buf, uint32_t dev_num);
void cmd_disp_exec_exit(uint8_t *resp_buf);
void cmd_disp_exec_ret(uint8_t *resp_buf);

#endif /* __UTIL_UUT_CMD_H */
