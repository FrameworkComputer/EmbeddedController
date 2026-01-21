/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TI TPS6699X PDC FW update code
 */

#include "drivers/pdc.h"
#include "tps6699x_cmd.h"
#include "tps6699x_reg.h"
#include "usbc/pdc_power_mgmt.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/base64.h>

LOG_MODULE_DECLARE(tps6699x, CONFIG_USBC_LOG_LEVEL);

#define TPS_4CC_MAX_DURATION K_MSEC(1200)
#define TPS_4CC_POLL_DELAY K_USEC(200)
#define TPS_RESET_DELAY K_MSEC(2000)
#define TPS_TFUI_HEADER_DELAY K_MSEC(200)
#define TPS_TFUS_BOOTLOADER_ENTRY_DELAY K_MSEC(500)

/* LCOV_EXCL_START - non-shipping code */
static struct {
	const struct device *pdc_dev;
	struct i2c_dt_spec pdc_i2c;
	size_t bytes_streamed;
} ctx;

struct tfu_initiate {
	uint16_t num_blocks;
	uint16_t data_block_size;
	uint16_t timeout_secs;
	uint16_t broadcast_address;
} __attribute__((__packed__));

struct tfu_download {
	uint16_t num_blocks;
	uint16_t data_block_size;
	uint16_t timeout_secs;
	uint16_t broadcast_address;
} __attribute__((__packed__));

/*
 * Complete uses custom values for switch/copy instead of true false.
 * Write these values to the register instead of true/false.
 */
#define DO_SWITCH 0xAC
#define DO_COPY 0xAC
struct tfu_complete {
	uint8_t do_switch;
	uint8_t do_copy;
} __attribute__((__packed__));

#define GAID_MAGIC_VALUE 0xAC
union gaid_params_t {
	struct {
		uint8_t switch_banks;
		uint8_t copy_banks;
	} __packed;
	uint8_t raw[2];
};

struct tfu_query {
	uint8_t bank;
	uint8_t cmd;
} __attribute__((__packed__));

struct tfu_query_output {
	uint8_t result;
	uint8_t tfu_state;
	uint8_t complete_image;
	uint16_t blocks_written;
	uint8_t header_block_status;
	uint8_t per_block_status[12];
	uint8_t num_header_bytes_written;
	uint8_t num_data_bytes_written;
	uint8_t num_appconfig_bytes_written;
} __attribute__((__packed__));

/* The string length of a base64 encoded message including padding. */
#define BASE64_LENGTH(n) ((4 * ((n) / 3) + 3) & ~3)
#define LEN_TFUi sizeof(struct tfu_initiate)
#define LEN_TFUd sizeof(struct tfu_download)
#define LEN_STREAM (66) /* Broadcast addr + 64-byte data */
#define BASE64_LEN_TFUi BASE64_LENGTH(LEN_TFUi)
#define BASE64_LEN_TFUd BASE64_LENGTH(LEN_TFUd)
#define BASE64_LEN_STREAM BASE64_LENGTH(LEN_STREAM)

/**
 * @brief Convert a 4CC command/task enum to a NUL-terminated printable string
 *
 * @param task The 4CC task enum
 * @param str_out Pointer to a char array capable of holding 5 characters where
 *        the output will be written to.
 */
static void command_task_to_string(enum command_task task, char str_out[5])
{
	if (task == 0) {
		const char no_task[5] = "0000";
		strncpy(str_out, no_task, strlen(no_task));
		return;
	}

	str_out[0] = (((uint32_t)task) >> 0);
	str_out[1] = (((uint32_t)task) >> 8);
	str_out[2] = (((uint32_t)task) >> 16);
	str_out[3] = (((uint32_t)task) >> 24);
	str_out[4] = '\0';
}

static int run_task_sync(const struct i2c_dt_spec *i2c, enum command_task task,
			 union reg_data *cmd_data, uint8_t *user_buf)
{
	k_timepoint_t timeout;
	union reg_command cmd;
	int rv;
	char task_str[5];

	command_task_to_string(task, task_str);

	/* Set up self-contained synchronous command call */
	if (cmd_data) {
		rv = tps_rw_data_for_cmd1(i2c, cmd_data, I2C_MSG_WRITE);
		if (rv) {
			LOG_ERR("Cannot set command data for '%s' (%d)",
				task_str, rv);
			return rv;
		}
	}

	cmd.command = task;

	rv = tps_rw_command_for_i2c1(i2c, &cmd, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Cannot set command for '%s' (%d)", task_str, rv);
		return rv;
	}

	/* Poll for successful completion */
	timeout = sys_timepoint_calc(TPS_4CC_MAX_DURATION);

	while (1) {
		k_sleep(TPS_4CC_POLL_DELAY);

		rv = tps_rw_command_for_i2c1(i2c, &cmd, I2C_MSG_READ);
		if (rv) {
			LOG_ERR("Cannot poll command status for '%s' (%d)",
				task_str, rv);
			return rv;
		}

		if (cmd.command == 0) {
			/* Command complete */
			break;
		} else if (cmd.command == 0x444d4321) {
			/* Unknown command ("!CMD") */
			LOG_ERR("Command '%s' is invalid", task_str);
			return -1;
		}

		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("Command '%s' timed out", task_str);
			return -ETIMEDOUT;
		}
	}

	LOG_INF("Command '%s' finished...", task_str);

	/* Read out success code */
	union reg_data cmd_data_check;

	rv = tps_rw_data_for_cmd1(i2c, &cmd_data_check, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Cannot get command result status for '%s' (%d)",
			task_str, rv);
		return rv;
	}

	/* Data byte offset 0 is the return error code */
	if (cmd_data_check.data[0] != 0) {
		LOG_ERR("Command '%s' failed. Chip says %02x", task_str,
			cmd_data_check.data[0]);
		return rv;
	}

	LOG_ERR("Command '%s' succeeded!!", task_str);

	/* Provide response data to user if a buffer is provided */
	if (user_buf != NULL) {
		memcpy(user_buf, cmd_data_check.data,
		       sizeof(cmd_data_check.data));
	}

	return 0;
}

static int do_reset_pdc(const struct i2c_dt_spec *i2c)
{
	union reg_data cmd_data;
	union gaid_params_t params;
	int rv;

	/* Default behavior is to switch banks. */
	params.switch_banks = GAID_MAGIC_VALUE;
	params.copy_banks = 0;

	memcpy(cmd_data.data, &params, sizeof(params));

	rv = run_task_sync(i2c, COMMAND_TASK_GAID, &cmd_data, NULL);

	if (rv == 0) {
		k_sleep(TPS_RESET_DELAY);
	}

	return rv;
}

static int tfus_run(const struct i2c_dt_spec *i2c)
{
	int ret;

	union reg_command cmd = {
		.command = COMMAND_TASK_TFUS,
	};

	/* Make three attempts to run the TFUs command to start FW update. */
	for (int attempts = 0; attempts < 3; attempts++) {
		ret = tps_rw_command_for_i2c1(i2c, &cmd, I2C_MSG_WRITE);
		if (ret == 0) {
			break;
		}

		k_sleep(K_MSEC(100));
	}

	if (ret) {
		LOG_ERR("Cannot write TFUs command (%d)", ret);
		return ret;
	}

	/* Wait 500ms for entry to bootloader mode, per datasheet */
	k_sleep(TPS_TFUS_BOOTLOADER_ENTRY_DELAY);

	/* Allow up to an additional 200ms */
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(200));

	while (1) {
		/* Check mode register for "F211" value */
		union reg_mode mode;

		ret = tps_rd_mode(i2c, &mode);

		if (ret == 0) {
			/* Got a mode result */
			if (memcmp("F211", mode.data, sizeof(mode.data)) == 0) {
				LOG_INF("TFUs complete, got F211");
				return 0;
			}

			/* Wrong mode, continue re-trying */
			LOG_ERR("TFUs failed! Mode is '%c%c%c%c'", mode.data[0],
				mode.data[1], mode.data[2], mode.data[3]);
		} else {
			/* I2C error, continue re-trying */
			LOG_ERR("Cannot read mode reg (%d)", ret);
		}

		if (sys_timepoint_expired(timeout)) {
			return -ETIMEDOUT;
		}

		k_sleep(K_MSEC(50));
	}
}

static int pdc_tps6699x_fwup_start(const struct device *dev)
{
	struct pdc_hw_config_t hw_config;
	int rv;

	if (ctx.pdc_dev) {
		LOG_ERR("FWUP session already in progress");
		return -EBUSY;
	}

	/* Shut down the PDC stack */
	rv = pdc_power_mgmt_set_comms_state(false);
	if (rv == -EALREADY) {
		LOG_INF("PDC stack already suspended");
	} else if (rv) {
		LOG_ERR("Cannot suspend PDC stack: %d", rv);
		return rv;
	}

	/* Get I2C info */
	rv = pdc_get_hw_config(dev, &hw_config);
	if (rv) {
		LOG_ERR("Cannot get PDC I2C info: %d", rv);
		return rv;
	}

	ctx.pdc_i2c = hw_config.i2c;

	/* Enter bootloader mode */
	rv = tfus_run(&ctx.pdc_i2c);
	if (rv) {
		LOG_ERR("Cannot enter bootloader mode (%d)", rv);
		return rv;
	}

	/* Ready for FW transfer */
	ctx.pdc_dev = dev;
	ctx.bytes_streamed = 0;

	return 0;
}

static int pdc_tps6699x_fwup_send_initiate(uint8_t *buffer, size_t buffer_len)
{
	union reg_data cmd_data;
	union reg_data rbuf;
	int rv;

	if (ctx.pdc_dev == NULL) {
		LOG_ERR("No FWUP session in progress");
		return -ENODEV;
	}

	if (buffer == NULL || buffer_len != LEN_TFUi) {
		LOG_ERR("Given data does not match TFUi format");
		return -EINVAL;
	}

	memcpy(cmd_data.data, buffer, buffer_len);
	rv = run_task_sync(&ctx.pdc_i2c, COMMAND_TASK_TFUI, &cmd_data,
			   rbuf.raw_value);
	if (rv < 0 || rbuf.data[0] != 0) {
		LOG_ERR("Failed to run TFUi. rv=%d, rbuf[0]=%u", rv,
			rbuf.data[0]);
		return rv;
	}

	/* Reset bytes written so we can track data we're streaming next */
	ctx.bytes_streamed = 0;

	return 0;
}

static int pdc_tps6699x_fwup_send_block(uint8_t *buffer, size_t buffer_len)
{
	union reg_data cmd_data;
	union reg_data rbuf;
	int rv;

	if (ctx.pdc_dev == NULL) {
		LOG_ERR("No FWUP session in progress");
		return -ENODEV;
	}

	if (buffer == NULL || buffer_len != LEN_TFUd) {
		LOG_ERR("Given data does not match TFUd format");
		return -EINVAL;
	}

	memcpy(cmd_data.data, buffer, buffer_len);
	rv = run_task_sync(&ctx.pdc_i2c, COMMAND_TASK_TFUD, &cmd_data,
			   rbuf.raw_value);
	if (rv < 0 || rbuf.data[0] != 0) {
		LOG_ERR("Failed to run TFUd. rv=%d, rbuf[0]=%u", rv,
			rbuf.data[0]);
		return rv;
	}

	/* Reset bytes written so we can track data we're streaming next */
	ctx.bytes_streamed = 0;

	return 0;
}

static int pdc_tps6699x_fwup_stream(uint8_t *buffer, size_t buffer_len)
{
	int rv;

	if (ctx.pdc_dev == NULL) {
		LOG_ERR("No FWUP session in progress");
		return -ENODEV;
	}

	if (buffer == NULL) {
		LOG_ERR("Given data does not match streaming format");
		return -EINVAL;
	}

	uint16_t broadcast_address = *(uint16_t *)buffer;
	uint8_t *data = &buffer[2];
	size_t data_len = buffer_len - 2;

	rv = tps_stream_data(&ctx.pdc_i2c, broadcast_address, data, data_len);
	ctx.bytes_streamed += data_len;

	return ctx.bytes_streamed;
}

static int pdc_tps6699x_tfuq(void)
{
	union reg_data cmd_data;
	union reg_data output;
	struct tfu_query *tfuq = (struct tfu_query *)cmd_data.data;
	int rv;

	tfuq->bank = 0;
	tfuq->cmd = 0;

	rv = run_task_sync(&ctx.pdc_i2c, COMMAND_TASK_TFUQ, &cmd_data,
			   output.data);
	if (rv < 0) {
		LOG_ERR("TFUq - Firmware update query failed (%d)", rv);
		return rv;
	}

	LOG_HEXDUMP_INF(output.data, sizeof(struct tfu_query_output),
			"TFUq raw data");

	return 0;
}

static int pdc_tps6699x_fwup_abort(void)
{
	int rv;
	union reg_data data;

	if (ctx.pdc_dev) {
		LOG_INF("TFU in progress - run TFUe to reset to normal firmware.");

		rv = run_task_sync(&ctx.pdc_i2c, COMMAND_TASK_TFUE, NULL,
				   data.raw_value);
		LOG_INF("TFUe rv=%d, result data byte=0x%02x", rv,
			data.data[0]);

		rv = do_reset_pdc(&ctx.pdc_i2c);
		if (rv) {
			LOG_ERR("PDC reset failed: %d", rv);
			LOG_ERR("Power cycle your board (battery cutoff and "
				"all external power)");

			/* Continue even if failed */
		}
	}

	/* Re-enable the PDC stack */
	LOG_INF("Re-enabling PDC stack");
	rv = pdc_power_mgmt_set_comms_state(true);
	if (rv == -EALREADY) {
		LOG_INF("PDC stack already running");
	} else if (rv) {
		LOG_ERR("PDC stack is stopped and cannot restart: %d", rv);

		/* Continue even if failed */
	}

	/* Reset session state */
	LOG_INF("Ending PDC FWUP session");
	memset(&ctx, 0, sizeof(ctx));

	return 0;
}

static int pdc_tps6699x_fwup_complete(void)
{
	union reg_data cmd_data;
	union reg_data rbuf;
	int rv;

	if (ctx.pdc_dev == NULL) {
		/* Need to start a FWUP session first */
		LOG_ERR("No FWUP session in progress");
		return -ENODEV;
	}

	/* Always dump TFUq before attempting completion. Failure here should
	 * result in an abort.
	 */
	if (pdc_tps6699x_tfuq() != 0) {
		return pdc_tps6699x_fwup_abort();
	}

	/* Finish update with a TFU copy. */
	struct tfu_complete tfuc;
	tfuc.do_switch = 0;
	tfuc.do_copy = DO_COPY;

	LOG_INF("Running TFUc [Switch: 0x%02x, Copy: 0x%02x]", tfuc.do_switch,
		tfuc.do_copy);
	memcpy(cmd_data.data, &tfuc, sizeof(tfuc));
	rv = run_task_sync(&ctx.pdc_i2c, COMMAND_TASK_TFUC, &cmd_data,
			   rbuf.data);

	if (rv < 0 || rbuf.data[0] != 0) {
		LOG_ERR("Failed 4cc task with result %d, rbuf.data[0] = %d", rv,
			rbuf.data[0]);
		return rv;
	}

	LOG_INF("TFUq bytes [Success: 0x%02x, State: 0x%02x, Complete: 0x%02x]",
		rbuf.data[1], rbuf.data[2], rbuf.data[3]);

	/* Wait TPS_RESET_DELAY for reset to complete. */
	k_sleep(TPS_RESET_DELAY);

	/* Re-enable the PDC stack */
	LOG_INF("Re-enabling PDC stack");
	rv = pdc_power_mgmt_set_comms_state(true);
	if (rv) {
		LOG_ERR("Cannot restart PDC stack: %d", rv);
		return rv;
	}

	LOG_INF("PDC FWUP successful");

	/* Reset session state */
	memset(&ctx, 0, sizeof(ctx));
	return 0;
}

static int cmd_pdc_tps_fwup_start(const struct shell *sh, size_t argc,
				  char **argv)
{
	int rv;
	uint8_t port;
	const struct device *dev;
	char *e;

	/* Get PD port number */
	port = strtoul(argv[1], &e, 0);
	if (*e || port >= pdc_power_mgmt_get_usb_pd_port_count()) {
		shell_error(sh, "TPS_FWUP: Invalid port");
		return -EINVAL;
	}

	dev = pdc_power_mgmt_get_port_pdc_driver(port);
	if (dev == NULL) {
		shell_error(sh,
			    "TI_FWUP: Cannot locate PDC driver for port C%u",
			    port);
		return -ENOENT;
	}

	rv = pdc_tps6699x_fwup_start(dev);
	if (rv) {
		shell_error(sh, "TPS_FWUP: Cannot start: %d", rv);
		return rv;
	}

	shell_info(sh, "TPS_FWUP: Started");
	return 0;
}

static int cmd_pdc_tps_fwup_send_initiate(const struct shell *sh, size_t argc,
					  char **argv)
{
	uint8_t decode_buffer[BASE64_LEN_TFUi];
	size_t decoded_byte_count = 0;
	int rv;

	rv = base64_decode(decode_buffer, sizeof(decode_buffer),
			   &decoded_byte_count, argv[1], strlen(argv[1]));

	if (rv) {
		shell_error(sh, "TPS_FWUP: Base64 format error: %d", rv);
		return rv;
	}

	rv = pdc_tps6699x_fwup_send_initiate(decode_buffer, decoded_byte_count);
	if (rv < 0) {
		shell_error(sh, "TPS_FWUP: Initiate (TFUi) error: %d", rv);
		return rv;
	}

	shell_info(sh, "TPS_FWUP: Send Initiate complete");
	return 0;
}

static int cmd_pdc_tps_fwup_send_block(const struct shell *sh, size_t argc,
				       char **argv)
{
	uint8_t decode_buffer[BASE64_LEN_TFUd];
	size_t decoded_byte_count = 0;
	int rv;

	rv = base64_decode(decode_buffer, sizeof(decode_buffer),
			   &decoded_byte_count, argv[1], strlen(argv[1]));

	if (rv) {
		shell_error(sh, "TPS_FWUP: Base64 format error: %d", rv);
		return rv;
	}

	rv = pdc_tps6699x_fwup_send_block(decode_buffer, decoded_byte_count);
	if (rv < 0) {
		shell_error(sh, "TPS_FWUP: Data block (TFUd) error: %d", rv);
		return rv;
	}

	shell_info(sh, "TPS_FWUP: Send Block complete");
	return 0;
}

static int cmd_pdc_tps_fwup_stream(const struct shell *sh, size_t argc,
				   char **argv)
{
	uint8_t decode_buffer[BASE64_LEN_STREAM];
	size_t decoded_byte_count = 0;
	int rv;

	rv = base64_decode(decode_buffer, sizeof(decode_buffer),
			   &decoded_byte_count, argv[1], strlen(argv[1]));

	if (rv) {
		shell_error(sh, "TPS_FWUP: Base64 format error: %d", rv);
		return rv;
	}

	rv = pdc_tps6699x_fwup_stream(decode_buffer, decoded_byte_count);
	if (rv < 0) {
		shell_error(sh, "TPS_FWUP: Streaming error: %d", rv);
		return rv;
	}

	shell_info(sh, "TPS_FWUP: Stream - bytes written: %d", rv);
	return 0;
}

static int cmd_pdc_tps_fwup_complete(const struct shell *sh, size_t argc,
				     char **argv)
{
	int rv;

	rv = pdc_tps6699x_fwup_complete();
	if (rv) {
		shell_error(sh, "TPS_FWUP: Cannot finish update: %d", rv);
		return rv;
	}

	shell_info(sh, "TPS_FWUP: Success");
	return 0;
}

static int cmd_pdc_tps_fwup_abort(const struct shell *sh, size_t argc,
				  char **argv)
{
	return pdc_tps6699x_fwup_abort();
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_pdc_tps_fwup_cmds,
	SHELL_CMD_ARG(start, NULL,
		      SHELL_HELP("Prepare the PDC for firmware download",
				 "<port>"),
		      cmd_pdc_tps_fwup_start, 2, 0),
	SHELL_CMD_ARG(
		send_initiate, NULL,
		SHELL_HELP("Send TFUi command with data to initiate update",
			   "<base64>"),
		cmd_pdc_tps_fwup_send_initiate, 2, 0),
	SHELL_CMD_ARG(
		send_block, NULL,
		SHELL_HELP("Send TFUd command with data to transfer block data",
			   "<base64>"),
		cmd_pdc_tps_fwup_send_block, 2, 0),
	SHELL_CMD_ARG(
		stream, NULL,
		SHELL_HELP(
			"Stream data for TFUi or TFUd after sending the command",
			"<base64>"),
		cmd_pdc_tps_fwup_stream, 2, 0),
	SHELL_CMD_ARG(
		complete, NULL,
		SHELL_HELP("Finalize the FW update and restart PD subsystem",
			   NULL),
		cmd_pdc_tps_fwup_complete, 1, 0),
	SHELL_CMD_ARG(
		abort, NULL,
		SHELL_HELP("Recover from a failed or interrupted update session",
			   NULL),
		cmd_pdc_tps_fwup_abort, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pdc_tps_fwup, &sub_pdc_tps_fwup_cmds,
		   "TI PDC firmware update commands", NULL);
/* LCOV_EXCL_STOP - non-shipping code */
