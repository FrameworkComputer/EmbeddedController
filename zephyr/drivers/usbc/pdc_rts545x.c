/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "uart.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <timer.h>
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "third_party/incbin/incbin.h"
INCBIN(rts545x_fw, "zephyr/drivers/usbc/rts545x_fw.bin");

#define PING_DELAY_MS 10
#define PING_RETRY_COUNT 200
#define PING_STATUS_MASK 0x3
#define PING_STATUS_COMPLETE 0x1
#define PING_STATUS_INVALID_FMT 0x3

#define MAX_COMMAND_SIZE 32
#define FW_CHUNKSIZE 29

#define RTS545X_VENDOR_CMD 0x01
#define RTS545X_FLASH_ERASE_CMD 0x03
#define RTS545X_FLASH_WRITE_0_64K_CMD 0x04
#define RTS545X_RESET_TO_FLASH_CMD 0x05
#define RTS545X_FLASH_WRITE_64K_128K_CMD 0x06
#define RTS545X_FLASH_WRITE_128K_192K_CMD 0x13
#define RTS545X_FLASH_WRITE_192K_256K_CMD 0x14
#define RTS545X_VALIDATE_ISP_CMD 0x16
#define RTS545X_GET_IC_STATUS_CMD 0x3A

enum flash_write_cmd_off {
	ADDR_L_OFF,
	ADDR_H_OFF,
	DATA_COUNT_OFF,
	DATA_OFF,
};

#define FLASH_WRITE_PROGRESS_INC (16 * 1024)

/*
 * Driver for the Realtek RTS545x Power Delivery Controller
 */
#define DT_DRV_COMPAT realtek_rts545x

LOG_MODULE_REGISTER(PDC_RTS545X, LOG_LEVEL_DBG);

#define ARGV_DEV 1

struct rts545x_config {
	/* I2C config */
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec irq_gpio;
};

/* Per instance runtime data */
struct rts545x_data {
	bool initialized;
};

struct rts5453_ic_status {
	uint8_t byte_count;
	uint8_t code_location;
	uint16_t reserved_0;
	uint8_t major_version;
	uint8_t minor_version;
	uint8_t patch_version;
	uint16_t reserved_1;
	uint8_t pd_typec_status;
	uint8_t vid_pid[4];
	uint8_t reserved_2;
	uint8_t flash_bank;
	uint8_t reserved_3[16];
} __attribute__((__packed__)) ic_status;

typedef int (*fw_update_op)(const struct shell *sh, size_t argc, char **argv);

static int rts545x_ping_status(const struct device *dev, uint8_t *status_byte)
{
	const struct rts545x_config *cfg = dev->config;
	struct i2c_msg ping_msg;
	int retry_count;
	int ret;

	ping_msg.buf = status_byte;
	ping_msg.len = 1;
	ping_msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	for (retry_count = 0; retry_count < PING_RETRY_COUNT; retry_count++) {
		ret = i2c_transfer_dt(&cfg->i2c, &ping_msg, 1);
		if (ret < 0)
			return ret;

		/* Command execution is complete */
		if ((*status_byte & PING_STATUS_MASK) == PING_STATUS_COMPLETE)
			return 0;

		/* Invalid command format */
		if ((*status_byte & PING_STATUS_MASK) ==
		    PING_STATUS_INVALID_FMT)
			return -EINVAL;

		k_msleep(PING_DELAY_MS);
	}
	return -ETIMEDOUT;
}

static int rts545x_block_out_transfer(const struct device *dev,
				      uint8_t cmd_code, size_t len,
				      uint8_t *write_data, uint8_t *status_byte)
{
	const struct rts545x_config *cfg = dev->config;
	int ret;
	struct i2c_msg write_msg;
	/* Command byte + Byte Count + Data[0..31] */
	uint8_t write_buf[MAX_COMMAND_SIZE + 2];

	if (len + 1 > ARRAY_SIZE(write_buf)) {
		return -EINVAL;
	}

	write_buf[0] = cmd_code;
	write_buf[1] = len;
	memcpy(&write_buf[2], write_data, len);

	write_msg.buf = write_buf;
	write_msg.len = len + 2;
	write_msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	ret = i2c_transfer_dt(&cfg->i2c, &write_msg, 1);

	if (ret != 0) {
		return ret;
	}

	if (cmd_code != RTS545X_RESET_TO_FLASH_CMD)
		return rts545x_ping_status(dev, status_byte);
	else
		return 0;
}

/*
 * Note - callers must first call rts545x_block_out_transfer() first.
 */
static int rts545x_block_in_transfer(const struct device *dev, size_t len,
				     uint8_t *read_data)
{
	const struct rts545x_config *cfg = dev->config;
	struct i2c_msg msg[2];
	uint8_t read_data_transfer_cmd = 0x80;

	msg[0].buf = &read_data_transfer_cmd;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = read_data;
	msg[1].len = len;
	msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, msg, 2);
}

/* Shell command support - start */
#define RT545X_DEV(inst) DEVICE_DT_GET(DT_DRV_INST(inst)),

static const struct device *const rts545x_devs[] = {
	DT_INST_FOREACH_STATUS_OKAY(RT545X_DEV)
};

static int rts545x_vendor_cmd_enable(const struct shell *sh, size_t argc,
				     char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t vendor_cmd_enable[] = { 0xDA, 0x0B, 0x01 };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, RTS545X_VENDOR_CMD,
					 ARRAY_SIZE(vendor_cmd_enable),
					 vendor_cmd_enable, &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, VENDOR_CMD_ENABLE ping status 0x%02x",
			    s_dev_name, ping_status);
	} else {
		shell_error(sh, "%s, VENDOR_CMD_ENABLE failed: %d", s_dev_name,
			    ret);
	}

	return ret;
}

static int rts545x_get_ic_status(const struct shell *sh, size_t argc,
				 char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t get_ic_status[] = { 0x00, 0x00, 0x1f };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, RTS545X_GET_IC_STATUS_CMD,
					 ARRAY_SIZE(get_ic_status),
					 get_ic_status, &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, GET_IC_CMD ping status 0x%02x", s_dev_name,
			    ping_status);
	} else {
		shell_error(sh, "%s, GET_IC_CMD failed: %d", s_dev_name, ret);
	}

	ret = rts545x_block_in_transfer(dev, sizeof(ic_status),
					(uint8_t *)&ic_status);

	if (ret != 0) {
		return ret;
	}

	shell_print(sh, "IC status:");
	shell_hexdump(sh, (uint8_t *)&ic_status, sizeof(ic_status));

	return ret;
}

static int rts545x_flash_access_enable(const struct shell *sh, size_t argc,
				       char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t flash_access_enable[] = { 0xDA, 0x0B, 0x03 };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, RTS545X_VENDOR_CMD,
					 ARRAY_SIZE(flash_access_enable),
					 flash_access_enable, &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, FLASH_ACCESS_ENABLE ping status 0x%02x",
			    s_dev_name, ping_status);
	} else {
		shell_error(sh, "%s, FLASH_ACCESS_ENABLE failed: %d",
			    s_dev_name, ret);
	}

	return ret;
}

static int rts545x_flash_write(const struct shell *sh, size_t argc, char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t flash_write[MAX_COMMAND_SIZE] = { 0 };
	uint8_t cmd;
	uint8_t ping_status;
	uint32_t offset = 0;
	uint8_t size = 0;
	uint32_t next_progress_update = FLASH_WRITE_PROGRESS_INC;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	while (offset < grts545x_fw_size) {
		if (ic_status.flash_bank) {
			/* Flash Bank 0 */
			if (offset < (64 * 1024))
				cmd = RTS545X_FLASH_WRITE_0_64K_CMD;
			else
				cmd = RTS545X_FLASH_WRITE_64K_128K_CMD;
		} else {
			/* Flash Bank 1 */
			if (offset < (64 * 1024))
				cmd = RTS545X_FLASH_WRITE_128K_192K_CMD;
			else
				cmd = RTS545X_FLASH_WRITE_192K_256K_CMD;
		}
		size = MIN(FW_CHUNKSIZE, grts545x_fw_size - offset);
		flash_write[ADDR_L_OFF] = (uint8_t)(offset & 0xff);
		flash_write[ADDR_H_OFF] = (uint8_t)((offset >> 8) & 0xff);
		flash_write[DATA_COUNT_OFF] = size;
		memcpy(&flash_write[DATA_OFF], &grts545x_fw_data[offset], size);

		/* Account for ADDR_L, ADDR_H, Write Data Count */
		ret = rts545x_block_out_transfer(dev, cmd, size + 3,
						 flash_write, &ping_status);
		if (ret) {
			shell_error(sh, "%s, FLASH_WRITE failed(%d) @off:0x%x",
				    s_dev_name, ret, offset);
		}

		if (offset > next_progress_update) {
			shell_print(sh, "%s, Updated 0x%x bytes, Writing...",
				    s_dev_name, offset);
			next_progress_update += FLASH_WRITE_PROGRESS_INC;
		}
		offset += size;
	}
	shell_print(sh, "%s, FLASH_WRITE complete\n", s_dev_name);
	return 0;
}

static int rts545x_validate_isp(const struct shell *sh, size_t argc,
				char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t validate_isp[] = { 0x01 };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, RTS545X_VALIDATE_ISP_CMD,
					 ARRAY_SIZE(validate_isp), validate_isp,
					 &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, VALIDATE_ISP ping status 0x%02x",
			    s_dev_name, ping_status);
	} else {
		shell_error(sh, "%s, VALIDATE_ISP failed: %d", s_dev_name, ret);
	}

	return ret;
}

static int rts545x_reset_to_flash(const struct shell *sh, size_t argc,
				  char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t reset_to_flash[] = { 0xDA, 0x0B, 0x01 };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, RTS545X_RESET_TO_FLASH_CMD,
					 ARRAY_SIZE(reset_to_flash),
					 reset_to_flash, &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, Reset to Flash passed", s_dev_name);
	} else {
		shell_error(sh, "%s, Reset to Flash failed: %d", s_dev_name,
			    ret);
	}

	return ret;
}

static int rts545x_flash_erase(const struct shell *sh, size_t argc, char **argv)
{
	char *s_dev_name = argv[ARGV_DEV];
	const struct device *dev;
	uint8_t flash_erase[] = { 0xDA, 0x0B, 0x00 };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, RTS545X_FLASH_ERASE_CMD,
					 ARRAY_SIZE(flash_erase), flash_erase,
					 &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, FLASH_ERASE_CMD ping status 0x%02x",
			    s_dev_name, ping_status);
	} else {
		shell_error(sh, "%s, FLASH_ERASE_CMD failed: %d", s_dev_name,
			    ret);
	}

	return ret;
}

static int rts545x_firmware_update(const struct shell *sh, size_t argc,
				   char **argv)
{
	int ret;
	char *s_dev_name = argv[ARGV_DEV];
	fw_update_op ops[] = {
		rts545x_vendor_cmd_enable,   rts545x_get_ic_status,
		rts545x_flash_access_enable, rts545x_flash_write,
		rts545x_vendor_cmd_enable,   rts545x_validate_isp,
		rts545x_reset_to_flash
	};

	for (int i = 0; i < ARRAY_SIZE(ops); i++) {
		ret = ops[i](sh, argc, argv);
		if (ret)
			return ret;
	}

	shell_print(sh, "%s, Firmware update done, sleeping 5s", s_dev_name);
	k_msleep(5000);
	return 0;
}

static void rts545x_get_name(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = NULL;

	if (idx < ARRAY_SIZE(rts545x_devs)) {
		dev = rts545x_devs[idx];
	}

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

static int rts545x_list(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "PDC list:");

	for (size_t i = 0; i < ARRAY_SIZE(rts545x_devs); i++) {
		const struct rts545x_config *cfg = rts545x_devs[i]->config;

		shell_print(sh, "  %p, %s, %02x", rts545x_devs[i],
			    rts545x_devs[i]->name, cfg->i2c.addr);
	}

	return 0;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, rts545x_get_name);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_pdc, SHELL_CMD_ARG(list, NULL, "List all PDCs", rts545x_list, 1, 0),
	SHELL_CMD_ARG(vendor_cmd_enable, &dsub_device_name,
		      "Send the VENDOR_CMD_ENABLE\n"
		      "Usage: vendor_cmd_enable <device>",
		      rts545x_vendor_cmd_enable, 2, 0),
	SHELL_CMD_ARG(get_ic_status, &dsub_device_name,
		      "Send the GET_IC_STATUS\n"
		      "Usage: get_ic_status <device>",
		      rts545x_get_ic_status, 2, 0),
	SHELL_CMD_ARG(flash_access_enable, &dsub_device_name,
		      "Send the FLASH_ACCESS_ENABLE\n"
		      "Usage: flash_access_enable <device>",
		      rts545x_flash_access_enable, 2, 0),
	SHELL_CMD_ARG(flash_write, &dsub_device_name,
		      "Write to the flash\n"
		      "Usage: flash_write <device>",
		      rts545x_flash_write, 2, 0),
	SHELL_CMD_ARG(flash_erase, &dsub_device_name,
		      "Erase the flash\n"
		      "Usage: flash_erase <device>",
		      rts545x_flash_erase, 2, 0),
	SHELL_CMD_ARG(flash_access_disable, &dsub_device_name,
		      "Send the FLASH_ACCESS_DISABLE\n"
		      "Usage: flash_access_disable <device>",
		      rts545x_vendor_cmd_enable, 2, 0),
	SHELL_CMD_ARG(validate_isp, &dsub_device_name,
		      "Validate the ISP\n"
		      "Usage: validate_isp <device>",
		      rts545x_validate_isp, 2, 0),
	SHELL_CMD_ARG(reset_to_flash, &dsub_device_name,
		      "Reset to flash\n"
		      "Usage: reset_to_flash <device>",
		      rts545x_reset_to_flash, 2, 0),
	SHELL_CMD_ARG(firmware_update, &dsub_device_name,
		      "Update the firmware\n"
		      "Usage: firmware_update <device>",
		      rts545x_firmware_update, 2, 0),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(pdc, &sub_pdc, "Commands for PDCs", NULL);
/* Shell command support - end */

static int rts545x_init(const struct device *dev)
{
	const struct rts545x_config *cfg = dev->config;
	struct rts545x_data *data = dev->data;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C is not ready");
		return -ENODEV;
	}

	data->initialized = true;

	return 0;
}

#define RTS545X_DEFINE(inst)                                                  \
	static struct rts545x_data rts545x_data_##inst;                       \
	static const struct rts545x_config rts545x_config_##inst = {          \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                            \
		.irq_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, irq_gpios, {}),    \
	};                                                                    \
	DEVICE_DT_INST_DEFINE(inst, rts545x_init, NULL, &rts545x_data_##inst, \
			      &rts545x_config_##inst, POST_KERNEL,            \
			      CONFIG_APPLICATION_INIT_PRIORITY,               \
			      NULL /* Driver API */);

DT_INST_FOREACH_STATUS_OKAY(RTS545X_DEFINE)
