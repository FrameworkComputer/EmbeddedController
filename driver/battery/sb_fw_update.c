/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery Firmware Update driver.
 * Ref: Common Smart Battery System Interface Specification v8.0.
 */

#include "battery.h"
#include "battery_smart.h"
#include "host_command.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"
#include "ec_commands.h"
#include "sb_fw_update.h"
#include "console.h"
#include "crc8.h"
#include "smbus.h"

#define CPRINTF(fmt, args...) cprintf(CC_I2C, fmt, ## args)

static struct ec_sb_fw_update_header sb_fw_hdr;

static int i2c_access_enable;

static int get_state(void)
{
	return sb_fw_hdr.subcmd;
}

static void set_state(int subcmd)
{
	sb_fw_hdr.subcmd = subcmd;
}

int sb_fw_update_in_progress(void)
{
	return i2c_access_enable;
}

/**
 * Check if a Smart Battery Firmware Update is protected.
 *
 * @return 1 if YES, 0 if NO.
 */
static int is_protected(void)
{
	int state = get_state();
	int vboot_mode = host_get_vboot_mode();

	if (vboot_mode == VBOOT_MODE_DEVELOPER)
		return 1;

	if (state == EC_SB_FW_UPDATE_PROTECT) {
		CPRINTF("firmware update is protected.\n");
		return 1;
	}

	return !i2c_access_enable;
}

static int prepare_update(struct host_cmd_handler_args *args)
{
	int rv = EC_RES_SUCCESS;
	args->response_size = 0;

	if (is_protected()) {
		CPRINTF("smbus cmd:%x data:%04x protect error\n",
			SB_FW_UPDATE_CMD_WRITE_WORD,
			SB_FW_UPDATE_CMD_WRITE_WORD_PREPARE);
		return EC_RES_INVALID_COMMAND;
	}

	set_state(EC_SB_FW_UPDATE_PREPARE);

	CPRINTF("smbus cmd:%x data:%04x\n",
			SB_FW_UPDATE_CMD_WRITE_WORD,
			SB_FW_UPDATE_CMD_WRITE_WORD_PREPARE);

	rv = smbus_write_word(I2C_PORT_BATTERY, BATTERY_ADDR,
		SB_FW_UPDATE_CMD_WRITE_WORD,
		SB_FW_UPDATE_CMD_WRITE_WORD_PREPARE);
	if (rv) {
		CPRINTF("smbus cmd:%x data:%04x access error\n",
			SB_FW_UPDATE_CMD_WRITE_WORD,
			SB_FW_UPDATE_CMD_WRITE_WORD_PREPARE);
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

static int begin_update(struct host_cmd_handler_args *args)
{
	int rv = EC_RES_SUCCESS;
	args->response_size = 0;

	if (is_protected()) {
		CPRINTF("smbus cmd:%x data:%04x protect error\n",
			SB_FW_UPDATE_CMD_WRITE_WORD,
			SB_FW_UPDATE_CMD_WRITE_WORD_UPDATE);
		return EC_RES_INVALID_COMMAND;
	}

	if (!i2c_access_enable)
		return EC_RES_ERROR;

	set_state(EC_SB_FW_UPDATE_BEGIN);

	rv = smbus_write_word(I2C_PORT_BATTERY, BATTERY_ADDR,
		SB_FW_UPDATE_CMD_WRITE_WORD,
		SB_FW_UPDATE_CMD_WRITE_WORD_UPDATE);
	if (rv) {
		CPRINTF("smbus cmd:%x data:%04x access error\n",
			SB_FW_UPDATE_CMD_WRITE_WORD,
			SB_FW_UPDATE_CMD_WRITE_WORD_UPDATE);
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

static int end_update(struct host_cmd_handler_args *args)
{
	int rv = EC_RES_SUCCESS;
	set_state(EC_SB_FW_UPDATE_END);

	args->response_size = 0;
	if (!i2c_access_enable)
		return EC_RES_ERROR;

	rv = smbus_write_word(I2C_PORT_BATTERY, BATTERY_ADDR,
		SB_FW_UPDATE_CMD_WRITE_WORD,
		SB_FW_UPDATE_CMD_WRITE_WORD_END);
	if (rv) {
		CPRINTF("smbus cmd:%x data:%x access error\n",
			SB_FW_UPDATE_CMD_WRITE_WORD,
			SB_FW_UPDATE_CMD_WRITE_WORD_END);
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}


static int get_info(struct host_cmd_handler_args *args)
{
	int rv = EC_RES_SUCCESS;
	uint8_t len = SB_FW_UPDATE_CMD_INFO_SIZE;

	struct ec_response_sb_fw_update *resp =
		(struct ec_response_sb_fw_update *)args->response;

	CPRINTF("smbus cmd:%x read battery info\n",
			SB_FW_UPDATE_CMD_READ_INFO);

	args->response_size = len;

	if (!i2c_access_enable) {
		CPRINTF("smbus cmd:%x rd info - protect error\n",
			SB_FW_UPDATE_CMD_READ_INFO);
		return EC_RES_ERROR;
	}

	rv = smbus_read_block(I2C_PORT_BATTERY, BATTERY_ADDR,
		SB_FW_UPDATE_CMD_READ_INFO, resp->info.data, &len);
	if (rv) {
		CPRINTF("smbus cmd:%x rd info - access error\n",
			SB_FW_UPDATE_CMD_READ_INFO);
		rv = EC_RES_ERROR;
	}
	return EC_RES_SUCCESS;
}

static int get_status(struct host_cmd_handler_args *args)
{
	int rv = EC_RES_SUCCESS;

	struct ec_response_sb_fw_update *resp =
		(struct ec_response_sb_fw_update *)args->response;

	uint16_t *p16 = (uint16_t *) resp->status.data;

	struct sb_fw_update_status *sts =
		(struct sb_fw_update_status *) resp->status.data;

	/* Enable smart battery i2c access */
	i2c_access_enable = 1;

	args->response_size = SB_FW_UPDATE_CMD_STATUS_SIZE;

	rv = smbus_read_word(I2C_PORT_BATTERY, BATTERY_ADDR,
		SB_FW_UPDATE_CMD_READ_STATUS, p16);

	if (rv == EC_ERROR_BUSY) {
		*p16 = 0;
		sts->busy = 1;
		return EC_RES_SUCCESS;
	} else if (rv) {
		CPRINTF("i2c cmd:%x read status error:0x%X\n",
				SB_FW_UPDATE_CMD_READ_STATUS, rv);
		return EC_RES_ERROR;
	}
	return EC_RES_SUCCESS;
}

static int set_protect(struct host_cmd_handler_args *args)
{
	set_state(EC_SB_FW_UPDATE_PROTECT);
	i2c_access_enable = 0;
	CPRINTF("firmware enter protect state !\n");
	args->response_size = 0;
	return EC_RES_SUCCESS;
}

static int write_block(struct host_cmd_handler_args *args)
{
	int rv = EC_RES_SUCCESS;
	struct ec_params_sb_fw_update *param =
		(struct ec_params_sb_fw_update *)args->params;

	args->response_size = 0;

	if (is_protected()) {
		CPRINTF("smbus write block protect error\n");
		return EC_RES_INVALID_COMMAND;
	}

	set_state(EC_SB_FW_UPDATE_WRITE);

	rv = smbus_write_block(I2C_PORT_BATTERY, BATTERY_ADDR,
			SB_FW_UPDATE_CMD_WRITE_BLOCK, param->write.data,
			SB_FW_UPDATE_CMD_WRITE_BLOCK_SIZE);
	if (rv) {
		CPRINTF("smbus write block access error\n");
		return EC_RES_ERROR;
	}
	return rv;
}

typedef int (*sb_fw_update_func)(struct host_cmd_handler_args *args);

static int sb_fw_update(struct host_cmd_handler_args *args)
{
	struct ec_sb_fw_update_header *hdr =
		(struct ec_sb_fw_update_header *)args->params;

	sb_fw_update_func sb_fw_update_tbl[] = {
		prepare_update,
		get_info,
		begin_update,
		write_block,
		end_update,
		get_status,
		set_protect
	};

	if (hdr->subcmd < EC_SB_FW_UPDATE_MAX)
		return sb_fw_update_tbl[hdr->subcmd](args);
	else
		return EC_RES_INVALID_PARAM;
}

DECLARE_HOST_COMMAND(EC_CMD_SB_FW_UPDATE,
		     sb_fw_update,
		     EC_VER_MASK(0));

