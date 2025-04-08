/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START - non-shipping code */

#include "drivers/pdc.h"
#include "pdc_rts54xx.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/base64.h>

LOG_MODULE_REGISTER(pdc_rts54_fwup, LOG_LEVEL_INF);

#define RTS54XX_MAX_CMD_SIZE 32
#define RTS54XX_FW_CHUNK_SIZE 29
#define RTS54XX_MAX_PACKET_SIZE (RTS54XX_MAX_CMD_SIZE + 2)

#define RTS54XX_PING_STATUS_TIMEOUT K_MSEC(10000)
#define RTS54XX_PING_STATUS_INTERVAL K_MSEC(10)
#define RTS54XX_RESET_DELAY K_MSEC(5000)

#define RTS54XX_VENDOR_CMD 0x01
#define RTS54XX_RESET_TO_FLASH_CMD 0x05
#define RTS54XX_FLASH_WRITE_0_64K_CMD 0x04
#define RTS54XX_FLASH_WRITE_64K_128K_CMD 0x06
#define RTS54XX_FLASH_WRITE_128K_192K_CMD 0x13
#define RTS54XX_FLASH_WRITE_192K_256K_CMD 0x14
#define RTS54XX_VALIDATE_ISP_CMD 0x16
#define RTS54XX_GET_IC_STATUS_CMD 0x3A

static struct {
	const struct device *pdc_dev;
	struct i2c_dt_spec pdc_i2c;
	struct pdc_info_t chip_info;
	size_t bytes_written;
} ctx;

static int rts54xx_ping_status(const struct i2c_dt_spec *i2c,
			       union ping_status_t *status_byte)
{
	struct i2c_msg ping_msg;
	k_timepoint_t timeout;
	int rv;

	__ASSERT(status_byte, "status_byte cannot be NULL");

	ping_msg.buf = &status_byte->raw_value;
	ping_msg.len = 1;
	ping_msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	timeout = sys_timepoint_calc(RTS54XX_PING_STATUS_TIMEOUT);

	while (1) {
		k_sleep(RTS54XX_PING_STATUS_INTERVAL);

		rv = i2c_transfer_dt(i2c, &ping_msg, 1);
		if (rv) {
			LOG_ERR("I2C error reading ping status: %d", rv);
			return rv;
		}

		/* Command execution is complete */
		if (status_byte->cmd_sts == CMD_DONE) {
			return 0;
		}

		/* Invalid command format */
		if (status_byte->cmd_sts == CMD_ERROR) {
			return -EINVAL;
		}

		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("Command timed out");
			return -ETIMEDOUT;
		}
	}
}

/**
 * @brief Perform a block write and wait for ping status
 */
static int rts54xx_block_out_transfer(const struct i2c_dt_spec *i2c, size_t len,
				      uint8_t *write_data,
				      union ping_status_t *status_byte)
{
	struct i2c_msg write_msg;
	int ret;

	if (len > RTS54XX_MAX_CMD_SIZE + 2) {
		return -EINVAL;
	}

	write_msg.buf = write_data;
	write_msg.len = len;
	write_msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	ret = i2c_transfer_dt(i2c, &write_msg, 1);
	if (ret) {
		return ret;
	}

	return rts54xx_ping_status(i2c, status_byte);
}

/**
 * @brief Perform a block read
 */
static int rts54xx_block_in_transfer(const struct i2c_dt_spec *i2c, size_t len,
				     uint8_t *read_data)
{
	struct i2c_msg msg[2];
	uint8_t read_data_transfer_cmd = RTS54XX_BLOCK_READ_CMD;

	msg[0].buf = &read_data_transfer_cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = read_data;
	msg[1].len = len;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(i2c, msg, 2);
}

/**
 * @brief Turn on vendor commands
 */
static int rts54xx_vendor_cmd_enable(const struct i2c_dt_spec *i2c)
{
	uint8_t vendor_cmd_enable[] = { RTS54XX_VENDOR_CMD, 3, 0xDA, 0x0B,
					0x01 };
	union ping_status_t ping_status;

	return rts54xx_block_out_transfer(i2c, ARRAY_SIZE(vendor_cmd_enable),
					  vendor_cmd_enable, &ping_status);
}

static int rts54xx_get_ic_status(const struct i2c_dt_spec *i2c,
				 struct pdc_info_t *info)
{
	uint8_t get_ic_status[] = { RTS54XX_GET_IC_STATUS_CMD, 3, 0x00, 0x00,
				    0x1F };
	uint8_t rx_buffer[31] = { 0 };
	union ping_status_t ping_status;
	int rv;

	if (info == NULL) {
		return -EINVAL;
	}

	rv = rts54xx_block_out_transfer(i2c, ARRAY_SIZE(get_ic_status),
					get_ic_status, &ping_status);
	if (rv) {
		LOG_ERR("GET_IC_STATUS block out transfer failed: %d (ping=0x%02x)",
			rv, ping_status.raw_value);
		return rv;
	}

	rv = rts54xx_block_in_transfer(
		i2c, MIN(ARRAY_SIZE(rx_buffer), ping_status.data_len),
		rx_buffer);
	if (rv) {
		LOG_ERR("GET_IC_STATUS block in transfer failed: %d (ping=0x%02x)",
			rv, ping_status.raw_value);
		return rv;
	}

	rts54xx_unpack_get_ic_status_response(rx_buffer, info);

	return 0;
}

static int rts54xx_enable_flash_access(const struct i2c_dt_spec *i2c)
{
	uint8_t flash_access_enable[] = { RTS54XX_VENDOR_CMD, 3, 0xDA, 0x0B,
					  0x03 };
	union ping_status_t ping_status;

	return rts54xx_block_out_transfer(i2c, ARRAY_SIZE(flash_access_enable),
					  flash_access_enable, &ping_status);
}

static int rts54xx_disable_flash_access(const struct i2c_dt_spec *i2c)
{
	return rts54xx_vendor_cmd_enable(i2c);
}

static int rts54xx_validate_flash(const struct i2c_dt_spec *i2c)
{
	uint8_t validate_flash[] = { RTS54XX_VALIDATE_ISP_CMD, 1, 0x01 };

	union ping_status_t ping_status;

	return rts54xx_block_out_transfer(i2c, ARRAY_SIZE(validate_flash),
					  validate_flash, &ping_status);
}

static int rts54xx_reset_to_flash(const struct i2c_dt_spec *i2c)
{
	uint8_t reset_to_flash[] = { RTS54XX_RESET_TO_FLASH_CMD, 3, 0xDA, 0x0B,
				     0x01 };
	union ping_status_t ping_status;

	return rts54xx_block_out_transfer(i2c, ARRAY_SIZE(reset_to_flash),
					  reset_to_flash, &ping_status);
}

/** Struct sent through the console interface as base64-encoded from host */
struct host_fwup_packet {
	/** Flash segment to write to: 0 for lower 64KiB, 1 for upper 64KiB */
	uint8_t segment;
	/** 16-bit offset within above flash segment. Little-endian. */
	uint16_t offset;
	/** Number of bytes in `chunk` */
	uint8_t len;
	/** Up to 29 bytes of FW payload to write in the given segment/offset */
	uint8_t chunk[RTS54XX_FW_CHUNK_SIZE];
} __packed;

BUILD_ASSERT(sizeof(struct host_fwup_packet) == 33);

struct rts54xx_flash_write_cmd {
	uint8_t cmd;
	uint8_t packet_len;
	uint16_t offset;
	uint8_t len;
	uint8_t payload[RTS54XX_FW_CHUNK_SIZE];
} __packed;

BUILD_ASSERT(sizeof(struct rts54xx_flash_write_cmd) == RTS54XX_MAX_PACKET_SIZE);

static int rts54xx_write_flash_chunk(const struct i2c_dt_spec *i2c,
				     uint8_t bank,
				     const struct host_fwup_packet *p)
{
	static const uint8_t cmds[2][2] = {
		/* Bank 0, lower and upper segments */
		{ RTS54XX_FLASH_WRITE_0_64K_CMD,
		  RTS54XX_FLASH_WRITE_64K_128K_CMD },
		/* Bank 1, lower and upper segments */
		{ RTS54XX_FLASH_WRITE_128K_192K_CMD,
		  RTS54XX_FLASH_WRITE_192K_256K_CMD },
	};

	union ping_status_t ping_status;
	int rv;

	if (p == NULL) {
		LOG_ERR("Packet ptr cannot be NULL");
		return -EINVAL;
	}

	if (bank > 1 || p->segment > 1 || p->len > RTS54XX_FW_CHUNK_SIZE) {
		LOG_ERR("Bad value(s) passed to %s: bank=%u, seg=%u, "
			"offset=%u, chunk_len=%u",
			__func__, bank, p->segment, p->offset, p->len);
		return -EINVAL;
	}

	struct rts54xx_flash_write_cmd cmd = {
		.cmd = cmds[bank][p->segment],
		.packet_len = (p->len + 3),
		.offset = p->offset, /* LE */
		.len = p->len,
	};

	memcpy(&cmd.payload, p->chunk, p->len);

	rv = rts54xx_block_out_transfer(i2c, p->len + 5, (uint8_t *)&cmd,
					&ping_status);
	if (rv == 0) {
		/* On success, return number of FW payload bytes written */
		return p->len;
	}

	LOG_ERR("Flash block out transfer failed: %d (ping_status=0x%02x)", rv,
		ping_status.raw_value);

	switch (rv) {
	case -EIO:
	case -EINVAL:
	case -ETIMEDOUT:
		return rv;
	}

	return -1;
}

static int pdc_rts54xx_fwup_start(const struct device *dev)
{
	struct pdc_bus_info_t bus_info;
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
	rv = pdc_get_bus_info(dev, &bus_info);
	if (rv) {
		LOG_ERR("Cannot get PDC I2C info: %d", rv);
		return rv;
	}

	ctx.pdc_i2c = bus_info.i2c;

	/* Enable vendor commands */
	LOG_INF("Enabling vendor commands");
	rv = rts54xx_vendor_cmd_enable(&ctx.pdc_i2c);
	if (rv) {
		LOG_ERR("Cannot enable vendor commands: %d", rv);
		return rv;
	}

	rv = rts54xx_get_ic_status(&ctx.pdc_i2c, &ctx.chip_info);
	if (rv) {
		LOG_ERR("Cannot get IC status: %d", rv);
		return rv;
	}

	LOG_INF("IC status: region=%s, bank=%u, FW %u.%u.%u",
		ctx.chip_info.is_running_flash_code ? "flash" : "rom",
		ctx.chip_info.running_in_flash_bank,
		PDC_FWVER_GET_MAJOR(ctx.chip_info.fw_version),
		PDC_FWVER_GET_MINOR(ctx.chip_info.fw_version),
		PDC_FWVER_GET_PATCH(ctx.chip_info.fw_version));

	/* Enable flash access */
	LOG_INF("Enabling flash access");
	rv = rts54xx_enable_flash_access(&ctx.pdc_i2c);
	if (rv) {
		LOG_ERR("Cannot enable flash access: %d", rv);
		return rv;
	}

	/* Ready for FW transfer */

	ctx.pdc_dev = dev;
	ctx.bytes_written = 0;

	return 0;
}

static int pdc_rts54xx_fwup_write(uint8_t *buffer, size_t buffer_len)
{
	struct host_fwup_packet *chunks = (struct host_fwup_packet *)buffer;
	size_t chunk_count = buffer_len / sizeof(struct host_fwup_packet);
	uint8_t flash_bank;
	int rv;

	if (ctx.pdc_dev == NULL) {
		/* Need to start a FWUP session w/ pdc_rts54xx_fwup_start() */
		LOG_ERR("No FWUP session in progress");
		return -ENODEV;
	}

	if (buffer == NULL || chunk_count == 0) {
		LOG_ERR("No chunks sent");
		return -EINVAL;
	}

	/* Determine which bank to write to. If the PDC is running ROM code, use
	 * bank 0. If running from flash code, use the opposite of the actively
	 * running bank.
	 */
	if (ctx.chip_info.is_running_flash_code) {
		flash_bank = !ctx.chip_info.running_in_flash_bank;
	} else {
		flash_bank = 0;
	}

	for (size_t c = 0; c < chunk_count; c++) {
		rv = rts54xx_write_flash_chunk(&ctx.pdc_i2c, flash_bank,
					       &chunks[c]);
		if (rv < 0) {
			LOG_ERR("Flash write failed: %d (bank=%u, seg=%u, offset=%u)",
				rv, flash_bank, chunks[c].segment,
				chunks[c].offset);
			return rv;
		}

		ctx.bytes_written += rv;
	}

	return ctx.bytes_written;
}

static int pdc_rts54xx_fwup_finish(void)
{
	int rv;

	if (ctx.pdc_dev == NULL) {
		/* Need to start a FWUP session w/ pdc_rts54xx_fwup_start() */
		LOG_ERR("No FWUP session in progress");
		return -ENODEV;
	}

	LOG_INF("Finishing update. Total FW bytes written: %u",
		ctx.bytes_written);

	/* Turn off flash access */
	LOG_INF("Disabling flash access");
	rv = rts54xx_disable_flash_access(&ctx.pdc_i2c);
	if (rv) {
		LOG_ERR("Cannot disable flash access: %d", rv);
		return rv;
	}

	/* Validate new image */
	LOG_INF("Validating new FW");
	rv = rts54xx_validate_flash(&ctx.pdc_i2c);
	if (rv) {
		LOG_ERR("FW validation failed: %d", rv);
		return rv;
	}

	/* Reset the PDC */
	LOG_INF("Resetting PDC");
	rv = rts54xx_reset_to_flash(&ctx.pdc_i2c);
	if (rv) {
		LOG_ERR("PDC reset failed: %d", rv);
		return rv;
	}

	LOG_INF("Waiting for PDC to reboot");
	k_sleep(RTS54XX_RESET_DELAY);

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

static int pdc_rts54xx_fwup_abort(void)
{
	int rv;

	if (ctx.pdc_dev) {
		/* Turn off flash access */
		LOG_INF("Session in progress - disable flash access");
		rv = rts54xx_disable_flash_access(&ctx.pdc_i2c);
		if (rv) {
			LOG_ERR("Cannot disable flash access: %d", rv);

			/* Continue even if failed */
		} else {
			LOG_INF("Flash access disabled");
		}

		/* Reset the PDC */
		LOG_INF("Resetting PDC");
		rv = rts54xx_reset_to_flash(&ctx.pdc_i2c);
		if (rv) {
			LOG_ERR("PDC reset failed: %d", rv);
			LOG_ERR("Power cycle your board (battery cutoff and "
				"all external power)");

			/* Continue even if failed */
		} else {
			LOG_INF("Waiting for PDC to reboot");
			k_sleep(RTS54XX_RESET_DELAY);
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

/*
 * Console interface
 */

static int cmd_pdc_rtk_fwup_start(const struct shell *sh, size_t argc,
				  char **argv)
{
	int rv;
	uint8_t port;
	const struct device *dev;
	char *e;

	/* Get PD port number */
	port = strtoul(argv[1], &e, 0);
	if (*e || port >= pdc_power_mgmt_get_usb_pd_port_count()) {
		shell_error(sh, "RTK_FWUP: Invalid port");
		return -EINVAL;
	}

	dev = pdc_power_mgmt_get_port_pdc_driver(port);
	if (dev == NULL) {
		shell_error(sh,
			    "RTK_FWUP: Cannot locate PDC driver for port C%u",
			    port);
		return -ENOENT;
	}

	rv = pdc_rts54xx_fwup_start(dev);
	if (rv) {
		shell_error(sh, "RTK_FWUP: Cannot start: %d", rv);
		return rv;
	}

	shell_info(sh, "RTK_FWUP: Started");
	return 0;
}

static int cmd_pdc_rtk_fwup_write(const struct shell *sh, size_t argc,
				  char **argv)
{
	uint8_t decode_buffer[66];
	size_t decoded_byte_count = 0;
	int rv;

	rv = base64_decode(decode_buffer, sizeof(decode_buffer),
			   &decoded_byte_count, argv[1], strlen(argv[1]));

	if (rv) {
		shell_error(sh, "RTK_FWUP: Base64 format error: %d", rv);
		return rv;
	}

	rv = pdc_rts54xx_fwup_write(decode_buffer, decoded_byte_count);
	if (rv < 0) {
		shell_error(sh, "RTK_FWUP: flash write error: %d", rv);
		return rv;
	}

	shell_info(sh, "RTK_FWUP: bytes written: %d", rv);
	return 0;
}

static int cmd_pdc_rtk_fwup_finish(const struct shell *sh, size_t argc,
				   char **argv)
{
	int rv;

	rv = pdc_rts54xx_fwup_finish();
	if (rv) {
		shell_error(sh, "RTK_FWUP: Cannot finish update: %d", rv);
		return rv;
	}

	shell_info(sh, "RTK_FWUP: Success");
	return 0;
}

static int cmd_pdc_rtk_fwup_abort(const struct shell *sh, size_t argc,
				  char **argv)
{
	return pdc_rts54xx_fwup_abort();
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_pdc_rtk_fwup_cmds,
	SHELL_CMD_ARG(start, NULL,
		      "Prepare the PDC for a firmware download\n"
		      "Usage: pdc_rtk_fwup start <port>",
		      cmd_pdc_rtk_fwup_start, 2, 0),
	SHELL_CMD_ARG(write, NULL,
		      "Write packets of 29 FW payload bytes to the PDC\n"
		      "Usage: pdc_rtk_fwup write <base64>",
		      cmd_pdc_rtk_fwup_write, 2, 0),
	SHELL_CMD_ARG(finish, NULL,
		      "Finalize the FW update and restart PD subsystem\n"
		      "Usage: pdc_rtk_fwup finish",
		      cmd_pdc_rtk_fwup_finish, 1, 0),
	SHELL_CMD_ARG(abort, NULL,
		      "Recover from a failed or interrupted update session\n"
		      "Usage: pdc_rtk_fwup abort",
		      cmd_pdc_rtk_fwup_abort, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pdc_rtk_fwup, &sub_pdc_rtk_fwup_cmds,
		   "Realtek PDC firmware update commands", NULL);

/* LCOV_EXCL_STOP */
