/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TI TPS6699X PDC FW update code
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(tps6699x, CONFIG_USBC_LOG_LEVEL);
#include "tps6699x_cmd.h"
#include "tps6699x_reg.h"

#define INCBIN_PREFIX g_
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "third_party/incbin/incbin.h"

/* TPS6699X_FW_ROOT is defined in this directory's CMakeLists.txt and points to
 * ${PLATFORM_EC}/zephyr/drivers/usbc
 */
INCBIN(tps6699x_fw, STRINGIFY(TPS6699X_FW_ROOT) "/tps6699x_19.8.0.bin");

#define TPS_4CC_MAX_DURATION K_MSEC(1200)
#define TPS_4CC_POLL_DELAY K_USEC(200)
#define TPS_RESET_DELAY K_MSEC(1000)
#define TPS_TFUD_BLOCK_DELAY K_MSEC(150)
#define TPS_TFUI_HEADER_DELAY K_MSEC(200)
#define TPS_TFUS_BOOTLOADER_ENTRY_DELAY K_MSEC(500)

/* LCOV_EXCL_START - non-shipping code */
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

struct tfu_query {
	uint8_t bank;
	uint8_t cmd;
} __attribute__((__packed__));

struct tps6699x_tfu_query_output {
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

/* Largest chunk we want to read before writing. */
#define MAX_READ_CHUNK_SIZE 0x4000

/* Send metadata with TFUi */
#define METADATA_OFFSET 0x4
#define METADATA_LENGTH 0x8

/* Stream header with i2c_stream AFTER TFUi */
#define HEADER_BLOCK_OFFSET 0xC
#define HEADER_BLOCK_LENGTH 0x800

/* Size of fw not including appconfig and header block is at this offset. */
#define FW_SIZE_OFFSET 0x4F8

/* Stream data blocks after you write metadata with TFUd. */
#define DATA_REGION_OFFSET 0x80C
#define DATA_BLOCK_SIZE 0x4000
#define DATA_METADATA_LENGTH 0x8
#define DATA_METADATA_OFFSET_AT(block)                        \
	(((DATA_BLOCK_SIZE + DATA_METADATA_LENGTH) * block) + \
	 DATA_REGION_OFFSET)
#define DATA_AT(block) (DATA_METADATA_OFFSET_AT(block) + DATA_METADATA_LENGTH)
#define TFUD_CHUNK_SIZE (64)

#define MAX_NUM_BLOCKS 12

#define GAID_MAGIC_VALUE 0xAC
union gaid_params_t {
	struct {
		uint8_t switch_banks;
		uint8_t copy_banks;
	} __packed;
	uint8_t raw[2];
};

static int get_and_print_device_info(const struct i2c_dt_spec *i2c)
{
	union reg_version version;
	int rv;

	rv = tps_rd_version(i2c, &version);
	if (rv != 0) {
		return rv;
	}

	return 0;
}

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
		strncpy(str_out, "0000", sizeof(*str_out));
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

/* Simply point to the offset in the file */
static int read_file_offset(int offset, const uint8_t **buf, int len)
{
	/* Exceed size of file. */
	if (offset + len > g_tps6699x_fw_size) {
		return -1;
	}

	*buf = &g_tps6699x_fw_data[offset];

	return len;
}

static int get_appconfig_offsets(uint16_t num_data_blocks, int *metadata_offset,
				 int *data_block_offset)
{
	int bytes_read;
	uint32_t *fw_size;

	bytes_read = read_file_offset(
		FW_SIZE_OFFSET, (const uint8_t **)&fw_size, sizeof(fw_size));

	if (bytes_read < 0) {
		LOG_ERR("Failed to read firmware size from binary: %d",
			bytes_read);
		return -1;
	}

	// The Application Configuration is stored at the following offset
	// FirmwareImageSize (Which excludes Header and App Config) + 0x800
	// (Header Block Size)
	// + (8 (Meta Data for Each Block including Header block) * Number of
	// Data block + 1)
	// + 4 (File Identifier)
	*metadata_offset = *fw_size + HEADER_BLOCK_LENGTH +
			   (DATA_METADATA_LENGTH * (num_data_blocks + 1)) +
			   METADATA_OFFSET;

	*data_block_offset = *metadata_offset + DATA_METADATA_LENGTH;

	return 0;
}

static int tfud_block(const struct i2c_dt_spec *i2c, uint8_t *fbuf,
		      int metadata_offset, int data_block_offset)
{
	struct tfu_download *tfud;
	union reg_data cmd_data;
	int bytes_read;
	uint8_t rbuf[64];
	int ret;

	/* First read the block metadata. */
	bytes_read = read_file_offset(metadata_offset, (const uint8_t **)&tfud,
				      DATA_METADATA_LENGTH);

	if (bytes_read < 0 || bytes_read != DATA_METADATA_LENGTH) {
		LOG_ERR("Failed to read block metadata. Wanted %d, got %d",
			DATA_METADATA_LENGTH, bytes_read);
		return -1;
	}

	LOG_INF("TFUd Info: nblks=%u, blksize=%u, timeout=%us, addr=%x",
		tfud->num_blocks, tfud->data_block_size, tfud->timeout_secs,
		tfud->broadcast_address);

	if (tfud->data_block_size > DATA_BLOCK_SIZE) {
		LOG_ERR("TFUd block size too big: 0x%x (max is 0x%x)",
			tfud->data_block_size, DATA_BLOCK_SIZE);
		return -1;
	}

	memcpy(&cmd_data.data, tfud, sizeof(*tfud));
	ret = run_task_sync(i2c, COMMAND_TASK_TFUD, &cmd_data, rbuf);

	if (ret < 0 || rbuf[0] != 0) {
		LOG_ERR("Failed to run TFUd. Ret=%d, rbuf[0] = %u", ret,
			rbuf[0]);
		return -1;
	}

	bytes_read = read_file_offset(data_block_offset,
				      (const uint8_t **)&fbuf,
				      tfud->data_block_size);

	if (bytes_read < 0 || bytes_read != tfud->data_block_size) {
		LOG_ERR("Failed to read block. Wanted %d, got %d",
			tfud->data_block_size, bytes_read);
		return -1;
	}

	/* Stream the data block */
	ret = tps_stream_data(i2c, tfud->broadcast_address, fbuf,
			      tfud->data_block_size);
	if (ret) {
		LOG_ERR("Downloading data block failed (%d)", ret);
		return -1;
	}

	/* Wait 150ms after each data block. */
	k_sleep(TPS_TFUD_BLOCK_DELAY);

	return 0;
}

static int tfuq_run(const struct i2c_dt_spec *i2c, uint8_t *output)
{
	union reg_data cmd_data;
	struct tfu_query *tfuq = (struct tfu_query *)cmd_data.data;
	tfuq->bank = 0;
	tfuq->cmd = 0;

	return run_task_sync(i2c, COMMAND_TASK_TFUQ, &cmd_data, output);
};

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

/**
 * @brief Temporary EC-based FW update routine
 *
 * @param dev Device pointer for the PDC to update (needed only once per chip)
 */
int tps6699x_do_firmware_update_internal(const struct i2c_dt_spec *i2c)
{
	int appconfig_metadata_offset, appconfig_data_offset;
	struct tfu_initiate *tfui;
	union reg_data cmd_data;
	int bytes_read = 0;
	uint8_t rbuf[64];
	int ret = 0;
	uint8_t *fbuf;

	/*
	 * Flow of operations for firmware update:
	 *   - TFUs: Start TFU process (puts device into bootloader mode)
	 *   - TFUi: Initiate firmware update. This also validates header.
	 *   - TFUd - Loop to download firmware.
	 *   - TFUc - Complete firmware update.
	 *
	 * To cancel or query current status, you can also do the following:
	 *   - TFUq: Query the TFU process
	 *   - TFUe: Cancel back to initial download state.
	 */

	/********************
	 * TFUs stage - enter bootloader code
	 */

	ret = tfus_run(i2c);
	if (ret) {
		LOG_ERR("Cannot enter bootloader mode (%d)", ret);
		return ret;
	}

	/********************
	 * TFUi stage
	 */

	/* Read metadata header. */
	bytes_read = read_file_offset(METADATA_OFFSET, (const uint8_t **)&tfui,
				      METADATA_LENGTH);
	if (bytes_read < 0) {
		LOG_ERR("Failed to read metadata. Wanted %d, got %d",
			METADATA_LENGTH, bytes_read);
		goto cleanup;
	}

	LOG_INF("Sending TFUi.");

	/* Write TFUi with header. */
	memcpy(cmd_data.data, tfui, sizeof(*tfui));
	ret = run_task_sync(i2c, COMMAND_TASK_TFUI, &cmd_data, rbuf);

	if (ret < 0 || rbuf[0] != 0) {
		LOG_ERR("Failed to run TFUi. Ret=%d, rbuf[0]=%u", ret, rbuf[0]);
		goto cleanup;
	}

	/* Read metadata buffer and stream at address given. */
	bytes_read = read_file_offset(HEADER_BLOCK_OFFSET,
				      (const uint8_t **)&fbuf,
				      HEADER_BLOCK_LENGTH);
	if (bytes_read < 0 || bytes_read != HEADER_BLOCK_LENGTH) {
		LOG_ERR("Failed to read header stream. Wanted %d but got %d",
			HEADER_BLOCK_LENGTH, bytes_read);
		goto cleanup;
	}

	LOG_INF("Streaming header to broadcast addr $%x",
		tfui->broadcast_address);

	ret = tps_stream_data(i2c, tfui->broadcast_address, fbuf,
			      HEADER_BLOCK_LENGTH);
	if (ret) {
		LOG_ERR("Streaming header failed (%d)", ret);
		goto cleanup;
	}

	LOG_INF("TFUi complete and header streamed. Number of blocks: %u",
		tfui->num_blocks);

	/* Wait 200ms after streaming header to do data block. */
	k_sleep(TPS_TFUI_HEADER_DELAY);

	/* Iterate through all image blocks. */
	for (int block = 0; block < tfui->num_blocks; ++block) {
		LOG_INF("Flashing block %d (%d/%u)", block, block + 1,
			tfui->num_blocks);
		ret = tfud_block(i2c, fbuf, DATA_METADATA_OFFSET_AT(block),
				 DATA_AT(block));
		if (ret) {
			LOG_ERR("Error while flashing block (%d)", ret);
			goto cleanup;
		}
	}

	LOG_INF("Flashing appconfig to block %d", tfui->num_blocks);
	if (get_appconfig_offsets(tfui->num_blocks, &appconfig_metadata_offset,
				  &appconfig_data_offset) < 0) {
		LOG_ERR("Failed to get appconfig offsets!");
		goto cleanup;
	}

	ret = tfud_block(i2c, fbuf, appconfig_metadata_offset,
			 appconfig_data_offset);
	if (ret) {
		LOG_ERR("Failed to write appconfig block (%d)", ret);
		goto cleanup;
	}

	/* Check the status with TFUq */
	ret = tfuq_run(i2c, rbuf);
	if (ret) {
		LOG_ERR("Could not query FW update status (%d)", ret);
		goto cleanup;
	}

	LOG_HEXDUMP_INF(rbuf, sizeof(struct tps6699x_tfu_query_output),
			"TFUq raw data");

	/* Finish update with a TFU copy. */
	struct tfu_complete tfuc;
	tfuc.do_switch = 0;
	tfuc.do_copy = DO_COPY;

	LOG_INF("Running TFUc [Switch: 0x%02x, Copy: 0x%02x]", tfuc.do_switch,
		tfuc.do_copy);
	memcpy(cmd_data.data, &tfuc, sizeof(tfuc));
	ret = run_task_sync(i2c, COMMAND_TASK_TFUC, &cmd_data, rbuf);

	if (ret < 0 || rbuf[0] != 0) {
		LOG_ERR("Failed 4cc task with result %d, rbuf[0] = %d", ret,
			rbuf[0]);
		goto cleanup;
	}

	LOG_INF("TFUq bytes [Success: 0x%02x, State: 0x%02x, Complete: 0x%02x]",
		rbuf[1], rbuf[2], rbuf[3]);

	/* Wait 1600ms for reset to complete. */
	k_msleep(1600);

	/* Confirm we're on the new firmware now. */
	get_and_print_device_info(i2c);

	return 0;

cleanup:
	ret = run_task_sync(i2c, COMMAND_TASK_TFUE, NULL, rbuf);

	LOG_ERR("Cleaning up resulted in ret=%d and result byte=0x%02x", ret,
		rbuf[0]);

	/* Reset and confirm we restored original firmware. */
	do_reset_pdc(i2c);
	get_and_print_device_info(i2c);

	return -1;
}
/* LCOV_EXCL_STOP - non-shipping code */
