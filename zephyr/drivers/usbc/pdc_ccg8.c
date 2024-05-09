/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Source file for PD CCG8 driver */

#include "usb_mux.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <drivers/pdc.h>
#include <usbc/utils.h>

#define PD_MAX_READ_WRITE_SIZE 4

/*
 * Retimer firmware update register: Send 0x01 command to go to firmware update
 * mode for all the retimers(single/dual port PD) controlled by a CCG8 PD.
 */
#define PD_ICL_CTRL_REG 0x0040
#define PD_ICL_CTRL_REG_LEN 1

#define DT_DRV_COMPAT infineon_ccg8

#define PD_CHIP_ENTRY(usbc_id, pd_id) \
	[USBC_PORT_NEW(usbc_id)] = DEVICE_DT_GET(pd_id),

#define PD_CHIP_FIND(usbc_id, pd_id)                          \
	COND_CODE_1(DT_NODE_HAS_COMPAT(pd_id, DT_DRV_COMPAT), \
		    (PD_CHIP_ENTRY(usbc_id, pd_id)), ())

#define PD_CHIP(usbc_id)                            \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, pdc), \
		    (PD_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, pdc))), ())

/* Generate device object array for available PDs */
static const struct device *pd_pow_config_array[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, PD_CHIP) };

LOG_MODULE_REGISTER(CCG8, LOG_LEVEL_ERR);

struct ccg8_config {
	/* I2C config */
	struct i2c_dt_spec i2c;
	/* Interrupt pin to trigger power event handlers */
	struct gpio_dt_spec int_gpio;
};

static int ccg_write(const struct device *dev, uint16_t reg, uint8_t len,
		     void *data)
{
	const struct ccg8_config *cfg = dev->config;
	uint8_t i2c_buf[PD_MAX_READ_WRITE_SIZE + 2];
	struct i2c_msg msg;

	/*
	 * Write sequence
	 * DEV_ADDR - REG_ID_0 - REG_ID_1 - DATA0 .. DATAn
	 */
	i2c_buf[0] = reg & 0x00ff;
	i2c_buf[1] = (reg & 0xff00) >> 8;
	memcpy(&i2c_buf[2], data, len);
	msg.buf = i2c_buf;
	msg.len = len + 2;
	msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(&cfg->i2c, &msg, 1);
}

static int ccg_read(const struct device *dev, uint16_t reg, uint8_t len,
		    void *buf)
{
	const struct ccg8_config *cfg = dev->config;

	return i2c_write_read_dt(&cfg->i2c, (uint8_t *)&reg, 2, buf, len);
}

static int ccg_init(const struct device *dev)
{
	const struct ccg8_config *cfg = dev->config;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C is not ready");
		return -ENODEV;
	}

	/*
	 * TODO(b:317292415) - Initialiaze CCG interrupt and callback functions
	 */

	return 0;
}

/* TODO(b:317338823) - The following UCSI APIs to be implemented later */
static int ccg_get_ucsi_version(const struct device *dev, uint16_t *version)
{
	return 0;
}

static int ccg_reset(const struct device *dev)
{
	return 0;
}

static int ccg_connector_reset(const struct device *dev,
			       union connector_reset_t reset_type)
{
	return 0;
}

static int ccg_get_capability(const struct device *dev,
			      struct capability_t *caps)
{
	return 0;
}

static int ccg_get_connector_capability(const struct device *dev,
					union connector_capability_t *caps)
{
	return 0;
}

static int ccg_set_ccom(const struct device *dev, enum ccom_t ccom)
{
	return 0;
}

static int ccg_set_uor(const struct device *dev, union uor_t uor)
{
	return 0;
}

static int ccg_set_pdr(const struct device *dev, union pdr_t pdr)
{
	return 0;
}

static int ccg_set_sink_path(const struct device *dev, bool en)
{
	return 0;
}

static int ccg_get_connector_status(const struct device *dev,
				    union connector_status_t *cs)
{
	return 0;
}

static int ccg_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
			enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			bool port_partner_pdo, uint32_t *pdos)
{
	return 0;
}

static int ccg_get_rdo(const struct device *dev, uint32_t *rdo)
{
	return 0;
}

static int ccg_set_rdo(const struct device *dev, uint32_t rdo)
{
	return 0;
}

static int ccg_get_error_status(const struct device *dev,
				union error_status_t *es)
{
	return 0;
}

static int ccg_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	return 0;
}

static int ccg_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
	return 0;
}

static int ccg_set_handler_cb(const struct device *dev,
			      pdc_cci_handler_cb_t cci_cb, void *cb_data)
{
	return 0;
}

static int ccg_read_power_level(const struct device *dev)
{
	return 0;
}

static int ccg_get_info(const struct device *dev, struct pdc_info_t *info,
			bool live)
{
	return 0;
}

static int ccg_set_power_level(const struct device *dev,
			       enum usb_typec_current_t tcc)
{
	return 0;
}

static int ccg_reconnect(const struct device *dev)
{
	return 0;
}

static int ccg_update_retimer(const struct device *dev, bool enable)
{
	return ccg_write(dev, PD_ICL_CTRL_REG, PD_ICL_CTRL_REG_LEN,
			 (uint8_t *)&enable);
}

static bool ccg_is_init_done(const struct device *dev)
{
	return false;
}

static const struct pdc_driver_api_t pdc_driver_api = {
	.is_init_done = ccg_is_init_done,
	.get_ucsi_version = ccg_get_ucsi_version,
	.reset = ccg_reset,
	.connector_reset = ccg_connector_reset,
	.get_capability = ccg_get_capability,
	.get_connector_capability = ccg_get_connector_capability,
	.set_ccom = ccg_set_ccom,
	.set_uor = ccg_set_uor,
	.set_pdr = ccg_set_pdr,
	.set_sink_path = ccg_set_sink_path,
	.get_connector_status = ccg_get_connector_status,
	.get_pdos = ccg_get_pdos,
	.get_rdo = ccg_get_rdo,
	.set_rdo = ccg_set_rdo,
	.get_error_status = ccg_get_error_status,
	.get_vbus_voltage = ccg_get_vbus_voltage,
	.get_current_pdo = ccg_get_current_pdo,
	.set_handler_cb = ccg_set_handler_cb,
	.read_power_level = ccg_read_power_level,
	.get_info = ccg_get_info,
	.set_power_level = ccg_set_power_level,
	.reconnect = ccg_reconnect,
	.update_retimer = ccg_update_retimer,
};

/*
 * TODO(b:317338824) - Move console command to application and make it generic
 * for all PD chips.
 */
#ifdef CONFIG_CONSOLE_CMD_PDC_CCG8
struct cmd_args {
	uint8_t port;
	uint16_t reg;
	uint8_t len;
	uint8_t data[PD_MAX_READ_WRITE_SIZE];
};

static int get_int_val(char *arg, void *reg, size_t val_size)
{
	char *e;
	uint16_t val = strtoul(arg, &e, 0);

	if (*e)
		return -EINVAL;

	switch (val_size) {
	case 1:
		if (val > UINT8_MAX)
			return -EINVAL;
		*(uint8_t *)reg = (uint8_t)val;
		break;
	case 2:
		*(uint16_t *)reg = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_port(char *arg, uint8_t *port)
{
	char *e;

	*port = strtoul(arg, &e, 0);
	if (*e || (*port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return -EINVAL;

	return 0;
}

static int process_arguments(const struct shell *sh, char **argv,
			     struct cmd_args *args)
{
	int rv;

	/* Convert port to int */
	rv = get_port(argv[1], &args->port);
	if (rv) {
		shell_error(sh, "Invalid port");
		return rv;
	}

	/* convert register to int */
	rv = get_int_val(argv[2], &args->reg, sizeof(args->reg));
	if (rv) {
		shell_error(sh, "Invalid register");
		return rv;
	}

	return 0;
}

static int cmd_read_register(const struct shell *sh, size_t argc, char **argv)
{
	struct cmd_args read_args;
	int rv;

	/* Process command arguments */
	rv = process_arguments(sh, argv, &read_args);
	if (rv)
		return rv;

	/* Convert register size to int */
	rv = get_int_val(argv[3], &read_args.len, sizeof(read_args.len));
	if (rv) {
		shell_error(sh, "Invalid length");
		return rv;
	}
	read_args.len = MIN(read_args.len, PD_MAX_READ_WRITE_SIZE);

	/* Read from PD registers */
	rv = ccg_read(pd_pow_config_array[read_args.port], read_args.reg,
		      read_args.len, read_args.data);
	if (rv) {
		shell_error(sh, "Read Failed, rv = %d", rv);
		return rv;
	}
	for (int i = 0; i < read_args.len; i++)
		shell_info(sh, "[%d] = %x", i, read_args.data[i]);

	return 0;
}

static int cmd_write_register(const struct shell *sh, size_t argc, char **argv)
{
	struct cmd_args write_args;
	int rv;

	/* Process command arguments */
	write_args.len = MIN(argc - 3, PD_MAX_READ_WRITE_SIZE);
	rv = process_arguments(sh, argv, &write_args);
	if (rv)
		return rv;

	/* Convert data to write to int */
	for (int i = 0; i < write_args.len; i++) {
		rv = get_int_val(argv[3 + i], &write_args.data[i],
				 sizeof(write_args.data[i]));
		if (rv) {
			shell_error(sh, "Invalid data");
			return rv;
		}
	}

	/* Write to PD registers */
	rv = ccg_write(pd_pow_config_array[write_args.port], write_args.reg,
		       write_args.len, write_args.data);
	if (rv) {
		shell_error(sh, "Write failed, rv = %d", rv);
		return rv;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	ccg_sub_cmds,
	SHELL_CMD_ARG(read, NULL,
		      "read from ccg PD\n"
		      "usage: read <port> <reg> <bytes>",
		      cmd_read_register, 4, 0),
	SHELL_CMD_ARG(write, NULL,
		      "write to ccg PD\n"
		      "usage: write <port> <reg> [<byte0>,...]",
		      cmd_write_register, 4, 3),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ccg, &ccg_sub_cmds, "CCG commands\n", NULL);
#endif

#define CCG8_DEFINE(inst)                                                     \
	static const struct ccg8_config ccg8_config##inst = {                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                            \
		.int_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
	};                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(inst, ccg_init, NULL, NULL, &ccg8_config##inst, \
			      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY,  \
			      &pdc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CCG8_DEFINE)
