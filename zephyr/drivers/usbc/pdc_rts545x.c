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

static int rts646x_ping_status(const struct device *dev, uint8_t *status_byte)
{
	const struct rts545x_config *cfg = dev->config;
	struct i2c_msg ping_msg;

	ping_msg.buf = status_byte;
	ping_msg.len = 1;
	ping_msg.flags = I2C_MSG_READ | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &ping_msg, 1);
}

static int rts545x_block_out_transfer(const struct device *dev,
				      uint8_t cmd_code, size_t len,
				      uint8_t *write_data, uint8_t *status_byte)
{
	const struct rts545x_config *cfg = dev->config;
	int ret;
	struct i2c_msg write_msg;
	uint8_t write_buf[16];

	if (len + 1 > ARRAY_SIZE(write_buf)) {
		return -EINVAL;
	}

	write_buf[0] = cmd_code;
	memcpy(&write_buf[1], write_data, len);

	write_msg.buf = write_buf;
	write_msg.len = len + 1;
	write_msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	ret = i2c_transfer_dt(&cfg->i2c, &write_msg, 1);

	if (ret != 0) {
		return ret;
	}

	return rts646x_ping_status(dev, status_byte);
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
	uint8_t vendor_cmd_enable[] = { 0x03, 0xDA, 0x0B, 0x01 };
	uint8_t ping_status;
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, 0x01,
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
	uint8_t get_ic_status[] = { 0x03, 0x00, 0x00, 0x1f };
	uint8_t ping_status;
	uint8_t ic_status[31];
	int ret;

	dev = device_get_binding(s_dev_name);

	if (dev == NULL) {
		shell_error(sh, "PDC: Device driver %s not found.", s_dev_name);
		return -ENODEV;
	}

	ret = rts545x_block_out_transfer(dev, 0x3a, ARRAY_SIZE(get_ic_status),
					 get_ic_status, &ping_status);

	if (ret == 0) {
		shell_print(sh, "%s, GET_IC_CMD ping status 0x%02x", s_dev_name,
			    ping_status);
	} else {
		shell_error(sh, "%s, GET_IC_CMD failed: %d", s_dev_name, ret);
	}

	ret = rts545x_block_in_transfer(dev, 31, ic_status);

	if (ret != 0) {
		return ret;
	}

	shell_print(sh, "IC status:");
	shell_hexdump(sh, ic_status, 31);

	return ret;
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
