/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ps8xxx_emul

#include <logging/log.h>
LOG_MODULE_REGISTER(ps8xxx_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <device.h>
#include <drivers/emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#include "tcpm/tcpci.h"

#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"

#include "driver/tcpm/ps8xxx.h"

#define PS8XXX_REG_MUX_IN_HPD_ASSERTION		MUX_IN_HPD_ASSERTION_REG

/** Run-time data used by the emulator */
struct ps8xxx_emul_data {
	/** Common I2C data used by "hidden" ports */
	struct i2c_common_emul_data p0_data;
	struct i2c_common_emul_data p1_data;
	struct i2c_common_emul_data gpio_data;

	/** Product ID of emulated device */
	int prod_id;
	/** Pointer to TCPCI emulator that is base for this emulator */
	const struct emul *tcpci_emul;

	/** Chip revision used by PS8805 */
	uint8_t chip_rev;
	/** Mux usb DCI configuration */
	uint8_t dci_cfg;
	/** GPIO control register value */
	uint8_t gpio_ctrl;
	/** HW revision used by PS8815 */
	uint16_t hw_rev;
};

/** Constant configuration of the emulator */
struct ps8xxx_emul_cfg {
	/** Phandle (name) of TCPCI emulator that is base for this emulator */
	const char *tcpci_emul;

	/** Common I2C configuration used by "hidden" ports */
	const struct i2c_common_emul_cfg p0_cfg;
	const struct i2c_common_emul_cfg p1_cfg;
	const struct i2c_common_emul_cfg gpio_cfg;
};

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_chip_rev(const struct emul *emul, uint8_t chip_rev)
{
	struct ps8xxx_emul_data *data = emul->data;

	data->chip_rev = chip_rev;
}

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_hw_rev(const struct emul *emul, uint16_t hw_rev)
{
	struct ps8xxx_emul_data *data = emul->data;

	data->hw_rev = hw_rev;
}

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_gpio_ctrl(const struct emul *emul, uint8_t gpio_ctrl)
{
	struct ps8xxx_emul_data *data = emul->data;

	data->gpio_ctrl = gpio_ctrl;
}

/** Check description in emul_ps8xxx.h */
uint8_t ps8xxx_emul_get_gpio_ctrl(const struct emul *emul)
{
	struct ps8xxx_emul_data *data = emul->data;

	return data->gpio_ctrl;
}

/** Check description in emul_ps8xxx.h */
uint8_t ps8xxx_emul_get_dci_cfg(const struct emul *emul)
{
	struct ps8xxx_emul_data *data = emul->data;

	return data->dci_cfg;
}

/** Check description in emul_ps8xxx.h */
int ps8xxx_emul_set_product_id(const struct emul *emul, uint16_t product_id)
{
	struct ps8xxx_emul_data *data = emul->data;

	if (product_id != PS8805_PRODUCT_ID &&
	    product_id != PS8815_PRODUCT_ID) {
		LOG_ERR("Setting invalid product ID 0x%x", product_id);
		return -EINVAL;
	}

	data->prod_id = product_id;
	tcpci_emul_set_reg(data->tcpci_emul, TCPC_REG_PRODUCT_ID, product_id);

	return 0;
}

/** Check description in emul_ps8xxx.h */
uint16_t ps8xxx_emul_get_product_id(const struct emul *emul)
{
	struct ps8xxx_emul_data *data = emul->data;

	return data->prod_id;
}

const struct emul *ps8xxx_emul_get_tcpci(const struct emul *emul)
{
	struct ps8xxx_emul_data *data = emul->data;

	return data->tcpci_emul;
}

/** Check description in emul_ps8xxx.h */
struct i2c_emul *ps8xxx_emul_get_i2c_emul(const struct emul *emul,
					  enum ps8xxx_emul_port port)
{
	const struct ps8xxx_emul_cfg *cfg = emul->cfg;
	struct ps8xxx_emul_data *data = emul->data;

	switch (port) {
	case PS8XXX_EMUL_PORT_0:
		return &data->p0_data.emul;
	case PS8XXX_EMUL_PORT_1:
		return &data->p1_data.emul;
	case PS8XXX_EMUL_PORT_GPIO:
		if (cfg->gpio_cfg.addr != 0) {
			return &data->gpio_data.emul;
		} else {
			return NULL;
		}
	default:
		return NULL;
	}
}

/**
 * @brief Function called for each byte of read message
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to device operations structure
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readded
 *
 * @return TCPCI_EMUL_CONTINUE to continue with default handler
 * @return TCPCI_EMUL_DONE to immedietly return success
 * @return TCPCI_EMUL_ERROR to immedietly return error
 */
static enum tcpci_emul_ops_resp ps8xxx_emul_tcpci_read_byte(
					const struct emul *emul,
					const struct tcpci_emul_dev_ops *ops,
					int reg, uint8_t *val, int bytes)
{
	uint16_t reg_val;

	switch (reg) {
	case PS8XXX_REG_FW_REV:
	case PS8XXX_REG_I2C_DEBUGGING_ENABLE:
	case PS8XXX_REG_MUX_IN_HPD_ASSERTION:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE0:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE1:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE2:
	case PS8XXX_REG_BIST_CONT_MODE_CTR:
		if (bytes != 0) {
			LOG_ERR("Reading byte %d from 1 byte register 0x%x",
				bytes, reg);
			return TCPCI_EMUL_ERROR;
		}

		tcpci_emul_get_reg(emul, reg, &reg_val);
		*val = reg_val & 0xff;
		return TCPCI_EMUL_DONE;
	default:
		return TCPCI_EMUL_CONTINUE;
	}
}

/**
 * @brief Function called for each byte of write message
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to device operations structure
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return TCPCI_EMUL_CONTINUE to continue with default handler
 * @return TCPCI_EMUL_DONE to immedietly return success
 * @return TCPCI_EMUL_ERROR to immedietly return error
 */
static enum tcpci_emul_ops_resp ps8xxx_emul_tcpci_write_byte(
					const struct emul *emul,
					const struct tcpci_emul_dev_ops *ops,
					int reg, uint8_t val, int bytes)
{
	uint16_t prod_id;

	tcpci_emul_get_reg(emul, TCPC_REG_PRODUCT_ID, &prod_id);

	switch (reg) {
	case PS8XXX_REG_RP_DETECT_CONTROL:
		/* This register is present only on PS8815 */
		if (prod_id != PS8815_PRODUCT_ID) {
			return TCPCI_EMUL_CONTINUE;
		}
	case PS8XXX_REG_I2C_DEBUGGING_ENABLE:
	case PS8XXX_REG_MUX_IN_HPD_ASSERTION:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE0:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE1:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE2:
	case PS8XXX_REG_BIST_CONT_MODE_CTR:
		if (bytes != 1) {
			LOG_ERR("Writing byte %d to 1 byte register 0x%x",
				bytes, reg);
			return TCPCI_EMUL_ERROR;
		}

		tcpci_emul_set_reg(emul, reg, val);
		return TCPCI_EMUL_DONE;
	default:
		return TCPCI_EMUL_CONTINUE;
	}
}

/**
 * @brief Function called on the end of write message
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to device operations structure
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return TCPCI_EMUL_CONTINUE to continue with default handler
 * @return TCPCI_EMUL_DONE to immedietly return success
 * @return TCPCI_EMUL_ERROR to immedietly return error
 */
static enum tcpci_emul_ops_resp ps8xxx_emul_tcpci_handle_write(
					const struct emul *emul,
					const struct tcpci_emul_dev_ops *ops,
					int reg, int msg_len)
{
	uint16_t prod_id;

	tcpci_emul_get_reg(emul, TCPC_REG_PRODUCT_ID, &prod_id);

	switch (reg) {
	case PS8XXX_REG_RP_DETECT_CONTROL:
		/* This register is present only on PS8815 */
		if (prod_id != PS8815_PRODUCT_ID) {
			return TCPCI_EMUL_CONTINUE;
		}
	case PS8XXX_REG_I2C_DEBUGGING_ENABLE:
	case PS8XXX_REG_MUX_IN_HPD_ASSERTION:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE0:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE1:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE2:
	case PS8XXX_REG_BIST_CONT_MODE_CTR:
		return TCPCI_EMUL_DONE;
	default:
		return TCPCI_EMUL_CONTINUE;
	}
}

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to TCPCI emulator
 * @param ops Pointer to device operations structure
 */
static void ps8xxx_emul_tcpci_reset(const struct emul *emul,
			     struct tcpci_emul_dev_ops *ops)
{
	tcpci_emul_set_reg(emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE, 0x31);
	tcpci_emul_set_reg(emul, PS8XXX_REG_MUX_IN_HPD_ASSERTION, 0x00);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_BYTE0, 0xff);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_BYTE1, 0x0f);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_BYTE2, 0x00);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_CTR, 0x00);
}

/** TCPCI PS8xxx operations */
static struct tcpci_emul_dev_ops ps8xxx_emul_ops = {
	.read_byte = ps8xxx_emul_tcpci_read_byte,
	.write_byte = ps8xxx_emul_tcpci_write_byte,
	.handle_write = ps8xxx_emul_tcpci_handle_write,
	.reset = ps8xxx_emul_tcpci_reset,
};

/**
 * @brief Get port associated with given "hidden" I2C device
 *
 * @param i2c_emul Pointer to "hidden" I2C device
 *
 * @return Port associated with given I2C device
 */
static enum ps8xxx_emul_port ps8xxx_emul_get_port(struct i2c_emul *i2c_emul)
{
	const struct ps8xxx_emul_cfg *cfg;
	const struct emul *emul;

	emul = i2c_emul->parent;
	cfg = emul->cfg;

	if (cfg->p0_cfg.addr == i2c_emul->addr) {
		return PS8XXX_EMUL_PORT_0;
	}

	if (cfg->p1_cfg.addr == i2c_emul->addr) {
		return PS8XXX_EMUL_PORT_1;
	}

	if (cfg->gpio_cfg.addr != 0 && cfg->gpio_cfg.addr == i2c_emul->addr) {
		return PS8XXX_EMUL_PORT_GPIO;
	}

	return PS8XXX_EMUL_PORT_INVAL;
}

/**
 * @brief Function called for each byte of read message
 *
 * @param i2c_emul Pointer to PS8xxx emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readded
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */
static int ps8xxx_emul_read_byte(struct i2c_emul *i2c_emul, int reg,
				 uint8_t *val, int bytes)
{
	struct ps8xxx_emul_data *data;
	enum ps8xxx_emul_port port;
	const struct emul *emul;
	uint16_t i2c_dbg_reg;

	emul = i2c_emul->parent;
	data = emul->data;

	tcpci_emul_get_reg(data->tcpci_emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE,
			   &i2c_dbg_reg);
	/* There is no need to enable I2C debug on PS8815 */
	if (data->prod_id != PS8815_PRODUCT_ID && i2c_dbg_reg & 0x1) {
		LOG_ERR("Accessing hidden i2c address without enabling debug");
		return -EIO;
	}

	port = ps8xxx_emul_get_port(i2c_emul);

	/* This is only 2 bytes register so handle it separately */
	if (data->prod_id == PS8815_PRODUCT_ID && port == PS8XXX_EMUL_PORT_1 &&
	    reg == PS8815_P1_REG_HW_REVISION) {
		if (bytes > 1) {
			LOG_ERR("Reading more than two bytes from HW rev reg");
			return -EIO;
		}

		*val = (data->hw_rev >> (bytes * 8)) & 0xff;
		return 0;
	}

	if (bytes != 0) {
		LOG_ERR("Reading more than one byte at once");
		return -EIO;
	}

	switch (port) {
	case PS8XXX_EMUL_PORT_0:
		if (data->prod_id == PS8805_PRODUCT_ID &&
		    reg == PS8805_P0_REG_CHIP_REVISION) {
			*val = data->chip_rev;
			return 0;
		}
		break;
	case PS8XXX_EMUL_PORT_1:
		/* DCI CFG is no available on PS8815 */
		if (data->prod_id != PS8815_PRODUCT_ID &&
		    reg == PS8XXX_P1_REG_MUX_USB_DCI_CFG) {
			*val = data->dci_cfg;
			return 0;
		}
	case PS8XXX_EMUL_PORT_GPIO:
		if (reg == PS8805_REG_GPIO_CONTROL) {
			*val = data->gpio_ctrl;
			return 0;
		}
	case PS8XXX_EMUL_PORT_INVAL:
		LOG_ERR("Invalid I2C address");
		return -EIO;
	}

	LOG_ERR("Reading from reg 0x%x which is WO or undefined", reg);
	return -EIO;
}

/**
 * @brief Function called for each byte of write message
 *
 * @param i2c_emul Pointer to PS8xxx emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int ps8xxx_emul_write_byte(struct i2c_emul *i2c_emul, int reg,
				  uint8_t val, int bytes)
{
	struct ps8xxx_emul_data *data;
	enum ps8xxx_emul_port port;
	const struct emul *emul;
	uint16_t i2c_dbg_reg;

	emul = i2c_emul->parent;
	data = emul->data;

	tcpci_emul_get_reg(data->tcpci_emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE,
			   &i2c_dbg_reg);
	/* There is no need to enable I2C debug on PS8815 */
	if (data->prod_id != PS8815_PRODUCT_ID && i2c_dbg_reg & 0x1) {
		LOG_ERR("Accessing hidden i2c address without enabling debug");
		return -EIO;
	}

	port = ps8xxx_emul_get_port(i2c_emul);

	if (bytes != 1) {
		LOG_ERR("Writing more than one byte at once");
		return -EIO;
	}

	switch (port) {
	case PS8XXX_EMUL_PORT_0:
		break;
	case PS8XXX_EMUL_PORT_1:
		/* DCI CFG is no available on PS8815 */
		if (data->prod_id != PS8815_PRODUCT_ID &&
		    reg == PS8XXX_P1_REG_MUX_USB_DCI_CFG) {
			data->dci_cfg = val;
			return 0;
		}
	case PS8XXX_EMUL_PORT_GPIO:
		if (reg == PS8805_REG_GPIO_CONTROL) {
			data->gpio_ctrl = val;
			return 0;
		}
	case PS8XXX_EMUL_PORT_INVAL:
		LOG_ERR("Invalid I2C address");
		return -EIO;
	}

	LOG_ERR("Writing to reg 0x%x which is RO or undefined", reg);
	return -EIO;
}

/**
 * @brief Set up a new PS8xxx emulator
 *
 * This should be called for each PS8xxx device that needs to be
 * emulated. It registers "hidden" I2C devices with the I2C emulation
 * controller and set PS8xxx device operations to associated TCPCI emulator.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int ps8xxx_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	const struct ps8xxx_emul_cfg *cfg = emul->cfg;
	struct ps8xxx_emul_data *data = emul->data;
	const struct device *i2c_dev;
	int ret;

	data->tcpci_emul = emul_get_binding(cfg->tcpci_emul);
	i2c_dev = parent;

	data->p0_data.emul.api = &i2c_common_emul_api;
	data->p0_data.emul.addr = cfg->p0_cfg.addr;
	data->p0_data.emul.parent = emul;
	data->p0_data.i2c = i2c_dev;
	data->p0_data.cfg = &cfg->p0_cfg;
	i2c_common_emul_init(&data->p0_data);

	data->p1_data.emul.api = &i2c_common_emul_api;
	data->p1_data.emul.addr = cfg->p1_cfg.addr;
	data->p1_data.emul.parent = emul;
	data->p1_data.i2c = i2c_dev;
	data->p1_data.cfg = &cfg->p1_cfg;
	i2c_common_emul_init(&data->p1_data);

	ret = i2c_emul_register(i2c_dev, emul->dev_label, &data->p0_data.emul);
	ret |= i2c_emul_register(i2c_dev, emul->dev_label, &data->p1_data.emul);

	if (cfg->gpio_cfg.addr != 0) {
		data->gpio_data.emul.api = &i2c_common_emul_api;
		data->gpio_data.emul.addr = cfg->gpio_cfg.addr;
		data->gpio_data.emul.parent = emul;
		data->gpio_data.i2c = i2c_dev;
		data->gpio_data.cfg = &cfg->gpio_cfg;
		i2c_common_emul_init(&data->gpio_data);
		ret |= i2c_emul_register(i2c_dev, emul->dev_label,
					 &data->gpio_data.emul);
	}

	tcpci_emul_set_dev_ops(data->tcpci_emul, &ps8xxx_emul_ops);
	ps8xxx_emul_tcpci_reset(data->tcpci_emul, &ps8xxx_emul_ops);

	tcpci_emul_set_reg(data->tcpci_emul, TCPC_REG_PRODUCT_ID,
			   data->prod_id);

	return ret;
}

#define PS8XXX_EMUL(n)							\
	static struct ps8xxx_emul_data ps8xxx_emul_data_##n = {		\
		.prod_id = PS8805_PRODUCT_ID,				\
		.p0_data = {						\
			.write_byte = ps8xxx_emul_write_byte,		\
			.read_byte = ps8xxx_emul_read_byte,		\
		},							\
		.p1_data = {						\
			.write_byte = ps8xxx_emul_write_byte,		\
			.read_byte = ps8xxx_emul_read_byte,		\
		},							\
		.gpio_data = {						\
			.write_byte = ps8xxx_emul_write_byte,		\
			.read_byte = ps8xxx_emul_read_byte,		\
		},							\
	};								\
									\
	static const struct ps8xxx_emul_cfg ps8xxx_emul_cfg_##n = {	\
		.tcpci_emul = DT_LABEL(DT_INST_PHANDLE(n, tcpci_i2c)),	\
		.p0_cfg = {						\
			.i2c_label = DT_INST_BUS_LABEL(n),		\
			.dev_label = DT_INST_LABEL(n),			\
			.data = &ps8xxx_emul_data_##n.p0_data,		\
			.addr = DT_INST_PROP(n, p0_i2c_addr),		\
		},							\
		.p1_cfg = {						\
			.i2c_label = DT_INST_BUS_LABEL(n),		\
			.dev_label = DT_INST_LABEL(n),			\
			.data = &ps8xxx_emul_data_##n.p1_data,		\
			.addr = DT_INST_PROP(n, p1_i2c_addr),		\
		},							\
		.gpio_cfg = {						\
			.i2c_label = DT_INST_BUS_LABEL(n),		\
			.dev_label = DT_INST_LABEL(n),			\
			.data = &ps8xxx_emul_data_##n.gpio_data,	\
			.addr = DT_INST_PROP(n, gpio_i2c_addr),		\
		},							\
	};								\
	EMUL_DEFINE(ps8xxx_emul_init, DT_DRV_INST(n),			\
		    &ps8xxx_emul_cfg_##n, &ps8xxx_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(PS8XXX_EMUL)
