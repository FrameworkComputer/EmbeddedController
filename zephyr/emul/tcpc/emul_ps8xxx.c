/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/ps8xxx.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT cros_ps8xxx_emul
#define PS8XXX_REG_MUX_IN_HPD_ASSERTION MUX_IN_HPD_ASSERTION_REG

LOG_MODULE_REGISTER(ps8xxx_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

/** Run-time data used by the emulator */
struct ps8xxx_emul_data {
	/** Common I2C data used by "hidden" ports */
	struct i2c_common_emul_data p0_data;
	struct i2c_common_emul_data p1_data;
	struct i2c_common_emul_data gpio_data;

	/** Product ID of emulated device */
	int prod_id;

	/** Chip revision used by PS8805 */
	uint8_t chip_rev;
	/** Mux usb DCI configuration */
	uint8_t dci_cfg;
	/** GPIO control register value */
	uint8_t gpio_ctrl;
	/** HW revision used by PS8815 */
	uint16_t hw_rev;
	/**
	 * Register ID to distinguish between the PS8815-A2 and PS8745-A2
	 * 0: Indicates this is an 8815-A2 chip
	 * 1: Indicates this is an 8745-A2 chip
	 */
	uint8_t reg_id;
};

/** Constant configuration of the emulator */
struct ps8xxx_emul_cfg {
	/** Common I2C configuration used by "hidden" ports */
	const struct i2c_common_emul_cfg p0_cfg;
	const struct i2c_common_emul_cfg p1_cfg;
	const struct i2c_common_emul_cfg gpio_cfg;
};

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_chip_rev(const struct emul *emul, uint8_t chip_rev)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	data->chip_rev = chip_rev;
}

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_reg_id(const struct emul *emul, enum ps8xxx_regid reg_id)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	data->reg_id = reg_id;
}

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_hw_rev(const struct emul *emul, uint16_t hw_rev)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	data->hw_rev = hw_rev;
}

/** Check description in emul_ps8xxx.h */
void ps8xxx_emul_set_gpio_ctrl(const struct emul *emul, uint8_t gpio_ctrl)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	data->gpio_ctrl = gpio_ctrl;
}

/** Check description in emul_ps8xxx.h */
uint8_t ps8xxx_emul_get_gpio_ctrl(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	return data->gpio_ctrl;
}

/** Check description in emul_ps8xxx.h */
uint8_t ps8xxx_emul_get_dci_cfg(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	return data->dci_cfg;
}

/** Check description in emul_ps8xxx.h */
int ps8xxx_emul_set_product_id(const struct emul *emul, uint16_t product_id)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	if (product_id != PS8805_PRODUCT_ID &&
	    product_id != PS8815_PRODUCT_ID) {
		LOG_ERR("Setting invalid product ID 0x%x", product_id);
		return -EINVAL;
	}

	data->prod_id = product_id;
	tcpci_emul_set_reg(emul, TCPC_REG_PRODUCT_ID, product_id);

	return 0;
}

/** Check description in emul_ps8xxx.h */
uint16_t ps8xxx_emul_get_product_id(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	return data->prod_id;
}

/** Check description in emul_ps8xxx.h */
struct i2c_common_emul_data *
ps8xxx_emul_get_i2c_common_data(const struct emul *emul,
				enum ps8xxx_emul_port port)
{
	const struct ps8xxx_emul_cfg *cfg = emul->cfg;
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;

	switch (port) {
	case PS8XXX_EMUL_PORT_0:
		return &data->p0_data;
	case PS8XXX_EMUL_PORT_1:
		return &data->p1_data;
	case PS8XXX_EMUL_PORT_GPIO:
		if (cfg->gpio_cfg.addr != 0) {
			return &data->gpio_data;
		} else {
			return NULL;
		}
	default:
		return NULL;
	}
}

/**
 * @brief Function called for each byte of read message from TCPC chip
 *
 * @param i2c_emul Pointer to PS8xxx emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */
static int ps8xxx_emul_tcpc_read_byte(const struct emul *emul, int reg,
				      uint8_t *val, int bytes)
{
	uint16_t reg_val;
	const struct i2c_emul *i2c_emul = emul->bus.i2c;

	LOG_DBG("PS8XXX TCPC 0x%x: read reg 0x%x", i2c_emul->addr, reg);

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
			return -EIO;
		}

		tcpci_emul_get_reg(emul, reg, &reg_val);
		*val = reg_val & 0xff;
		return 0;
	default:
		break;
	}

	return tcpci_emul_read_byte(emul, reg, val, bytes);
}

/**
 * @brief Function called for each byte of write message to TCPC chip
 *
 * @param emul Pointer to PS8xxx emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int ps8xxx_emul_tcpc_write_byte(const struct emul *emul, int reg,
				       uint8_t val, int bytes)
{
	uint16_t prod_id;
	struct i2c_emul *i2c_emul = emul->bus.i2c;

	LOG_DBG("PS8XXX TCPC 0x%x: write reg 0x%x", i2c_emul->addr, reg);

	tcpci_emul_get_reg(emul, TCPC_REG_PRODUCT_ID, &prod_id);

	switch (reg) {
	case PS8XXX_REG_RP_DETECT_CONTROL:
		/* This register is present only on PS8815 */
		if (prod_id != PS8815_PRODUCT_ID) {
			break;
		}
		__fallthrough;
	case PS8XXX_REG_I2C_DEBUGGING_ENABLE:
	case PS8XXX_REG_MUX_IN_HPD_ASSERTION:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE0:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE1:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE2:
	case PS8XXX_REG_BIST_CONT_MODE_CTR:
		if (bytes != 1) {
			LOG_ERR("Writing byte %d to 1 byte register 0x%x",
				bytes, reg);
			return -EIO;
		}

		tcpci_emul_set_reg(emul, reg, val);
		return 0;
	default:
		break;
	}

	return tcpci_emul_write_byte(emul, reg, val, bytes);
}

/**
 * @brief Function called on the end of write message to TCPC chip
 *
 * @param emul Pointer to PS8xxx emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int ps8xxx_emul_tcpc_finish_write(const struct emul *emul, int reg,
					 int msg_len)
{
	uint16_t prod_id;
	struct i2c_emul *i2c_emul = emul->bus.i2c;

	LOG_DBG("PS8XXX TCPC 0x%x: finish write reg 0x%x", i2c_emul->addr, reg);

	tcpci_emul_get_reg(emul, TCPC_REG_PRODUCT_ID, &prod_id);

	switch (reg) {
	case PS8XXX_REG_RP_DETECT_CONTROL:
		/* This register is present only on PS8815 */
		if (prod_id != PS8815_PRODUCT_ID) {
			break;
		}
		__fallthrough;
	case PS8XXX_REG_I2C_DEBUGGING_ENABLE:
	case PS8XXX_REG_MUX_IN_HPD_ASSERTION:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE0:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE1:
	case PS8XXX_REG_BIST_CONT_MODE_BYTE2:
	case PS8XXX_REG_BIST_CONT_MODE_CTR:
		return 0;
	default:
		break;
	}

	return tcpci_emul_handle_write(emul, reg, msg_len);
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        register from TCPC chip.
 *
 * @param emul Pointer to TCPCI emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int ps8xxx_emul_tcpc_access_reg(const struct emul *emul, int reg,
				       int bytes, bool read)
{
	return reg;
}

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to PS8xxx emulator
 */
static int ps8xxx_emul_tcpc_reset(const struct emul *emul)
{
	tcpci_emul_set_reg(emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE, 0x31);
	tcpci_emul_set_reg(emul, PS8XXX_REG_MUX_IN_HPD_ASSERTION, 0x00);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_BYTE0, 0xff);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_BYTE1, 0x0f);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_BYTE2, 0x00);
	tcpci_emul_set_reg(emul, PS8XXX_REG_BIST_CONT_MODE_CTR, 0x00);

	return tcpci_emul_reset(emul);
}

/**
 * @brief Function called for each byte of read message
 *
 * @param emul Pointer to PS8xxx emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 * @return -EIO on invalid read request
 */
static int ps8xxx_emul_read_byte_workhorse(const struct emul *emul, int reg,
					   uint8_t *val, int bytes,
					   enum ps8xxx_emul_port port)
{
	struct tcpc_emul_data *tcpc_data;
	struct ps8xxx_emul_data *data;
	struct i2c_emul *i2c_emul = emul->bus.i2c;
	uint16_t i2c_dbg_reg;

	LOG_DBG("PS8XXX 0x%x: read reg 0x%x", i2c_emul->addr, reg);

	tcpc_data = emul->data;
	data = tcpc_data->chip_data;

	tcpci_emul_get_reg(emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE, &i2c_dbg_reg);
	/* There is no need to enable I2C debug on PS8815 */
	if (data->prod_id != PS8815_PRODUCT_ID && i2c_dbg_reg & 0x1) {
		LOG_ERR("Accessing hidden i2c address without enabling debug");
		return -EIO;
	}

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
		if (data->prod_id == PS8815_PRODUCT_ID &&
		    reg == PS8815_P0_REG_ID) {
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
		__fallthrough;
	case PS8XXX_EMUL_PORT_GPIO:
		if (reg == PS8805_REG_GPIO_CONTROL) {
			*val = data->gpio_ctrl;
			return 0;
		}
		__fallthrough;
	case PS8XXX_EMUL_PORT_INVAL:
		LOG_ERR("Invalid I2C address");
		return -EIO;
	}

	LOG_ERR("Reading from reg 0x%x which is WO or undefined", reg);
	return -EIO;
}

static int ps8xxx_emul_p0_read_byte(const struct emul *emul, int reg,
				    uint8_t *val, int bytes)
{
	return ps8xxx_emul_read_byte_workhorse(emul, reg, val, bytes,
					       PS8XXX_EMUL_PORT_0);
}

static int ps8xxx_emul_p1_read_byte(const struct emul *emul, int reg,
				    uint8_t *val, int bytes)
{
	return ps8xxx_emul_read_byte_workhorse(emul, reg, val, bytes,
					       PS8XXX_EMUL_PORT_1);
}

static int ps8xxx_emul_gpio_read_byte(const struct emul *emul, int reg,
				      uint8_t *val, int bytes)
{
	return ps8xxx_emul_read_byte_workhorse(emul, reg, val, bytes,
					       PS8XXX_EMUL_PORT_GPIO);
}

/**
 * @brief Function called for each byte of write message
 *
 * @param emul Pointer to PS8xxx emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write request
 */
static int ps8xxx_emul_write_byte_workhorse(const struct emul *emul, int reg,
					    uint8_t val, int bytes,
					    enum ps8xxx_emul_port port)
{
	struct ps8xxx_emul_data *data;
	struct tcpc_emul_data *tcpc_data;
	const struct i2c_emul *i2c_emul = emul->bus.i2c;
	uint16_t i2c_dbg_reg;

	LOG_DBG("PS8XXX 0x%x: write reg 0x%x", i2c_emul->addr, reg);

	tcpc_data = emul->data;
	data = tcpc_data->chip_data;

	tcpci_emul_get_reg(emul, PS8XXX_REG_I2C_DEBUGGING_ENABLE, &i2c_dbg_reg);
	/* There is no need to enable I2C debug on PS8815 */
	if (data->prod_id != PS8815_PRODUCT_ID && i2c_dbg_reg & 0x1) {
		LOG_ERR("Accessing hidden i2c address without enabling debug");
		return -EIO;
	}

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
		__fallthrough;
	case PS8XXX_EMUL_PORT_GPIO:
		if (reg == PS8805_REG_GPIO_CONTROL) {
			data->gpio_ctrl = val;
			return 0;
		}
		__fallthrough;
	case PS8XXX_EMUL_PORT_INVAL:
		LOG_ERR("Invalid I2C address");
		return -EIO;
	}

	LOG_ERR("Writing to reg 0x%x which is RO or undefined", reg);
	return -EIO;
}

static int ps8xxx_emul_p0_write_byte(const struct emul *emul, int reg,
				     uint8_t val, int bytes)
{
	return ps8xxx_emul_write_byte_workhorse(emul, reg, val, bytes,
						PS8XXX_EMUL_PORT_0);
}

static int ps8xxx_emul_p1_write_byte(const struct emul *emul, int reg,
				     uint8_t val, int bytes)
{
	return ps8xxx_emul_write_byte_workhorse(emul, reg, val, bytes,
						PS8XXX_EMUL_PORT_1);
}

static int ps8xxx_emul_gpio_write_byte(const struct emul *emul, int reg,
				       uint8_t val, int bytes)
{
	return ps8xxx_emul_write_byte_workhorse(emul, reg, val, bytes,
						PS8XXX_EMUL_PORT_GPIO);
}

static int i2c_ps8xxx_emul_transfer(const struct emul *target,
				    struct i2c_msg *msgs, int num_msgs,
				    int addr)
{
	struct tcpc_emul_data *tcpc_data = target->data;
	struct ps8xxx_emul_data *ps8_xxx_data = tcpc_data->chip_data;
	const struct ps8xxx_emul_cfg *ps8_xxx_cfg = target->cfg;
	struct i2c_common_emul_data *common_data;

	/* The chip itself */
	if (addr == tcpc_data->i2c_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg =
			&tcpc_data->i2c_cfg;
		common_data = &tcpc_data->tcpci_ctx->common;

		return i2c_common_emul_transfer_workhorse(
			target, common_data, common_cfg, msgs, num_msgs, addr);
	}
	/* Subchips */
	else if (addr == ps8_xxx_cfg->gpio_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg =
			&ps8_xxx_cfg->gpio_cfg;
		common_data = &ps8_xxx_data->gpio_data;

		return i2c_common_emul_transfer_workhorse(
			target, common_data, common_cfg, msgs, num_msgs, addr);
	} else if (addr == ps8_xxx_cfg->p0_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg =
			&ps8_xxx_cfg->p0_cfg;
		common_data = &ps8_xxx_data->p0_data;

		return i2c_common_emul_transfer_workhorse(
			target, common_data, common_cfg, msgs, num_msgs, addr);
	} else if (addr == ps8_xxx_cfg->p1_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg =
			&ps8_xxx_cfg->p1_cfg;
		common_data = &ps8_xxx_data->p1_data;

		return i2c_common_emul_transfer_workhorse(
			target, common_data, common_cfg, msgs, num_msgs, addr);
	}

	LOG_ERR("Cannot map address %02x", addr);
	return -EIO;
}

struct i2c_emul_api i2c_ps8xxx_emul_api = {
	.transfer = i2c_ps8xxx_emul_transfer,
};

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
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct ps8xxx_emul_data *data = tcpc_data->chip_data;
	struct tcpci_ctx *tcpci_ctx = tcpc_data->tcpci_ctx;
	const struct ps8xxx_emul_cfg *cfg = emul->cfg;
	const struct device *i2c_dev;
	int ret = 0;

	i2c_dev = parent;

	tcpci_ctx->common.write_byte = ps8xxx_emul_tcpc_write_byte;
	tcpci_ctx->common.finish_write = ps8xxx_emul_tcpc_finish_write;
	tcpci_ctx->common.read_byte = ps8xxx_emul_tcpc_read_byte;
	tcpci_ctx->common.access_reg = ps8xxx_emul_tcpc_access_reg;

	tcpci_emul_i2c_init(emul, i2c_dev);

	data->p0_data.emul.api = &i2c_ps8xxx_emul_api;
	data->p0_data.emul.addr = cfg->p0_cfg.addr;
	data->p0_data.emul.target = emul;
	data->p0_data.i2c = i2c_dev;
	data->p0_data.cfg = &cfg->p0_cfg;
	i2c_common_emul_init(&data->p0_data);

	data->p1_data.emul.api = &i2c_ps8xxx_emul_api;
	data->p1_data.emul.addr = cfg->p1_cfg.addr;
	data->p1_data.emul.target = emul;
	data->p1_data.i2c = i2c_dev;
	data->p1_data.cfg = &cfg->p1_cfg;
	i2c_common_emul_init(&data->p1_data);

	/* Have to manually register "hidden" addressed chips under overarching
	 * ps8xxx
	 * TODO(b/240564574): Call EMUL_DEFINE for each "hidden" sub-chip.
	 */
	ret |= i2c_emul_register(i2c_dev, &data->p0_data.emul);
	ret |= i2c_emul_register(i2c_dev, &data->p1_data.emul);

	if (cfg->gpio_cfg.addr != 0) {
		data->gpio_data.emul.api = &i2c_ps8xxx_emul_api;
		data->gpio_data.emul.addr = cfg->gpio_cfg.addr;
		data->gpio_data.emul.target = emul;
		data->gpio_data.i2c = i2c_dev;
		data->gpio_data.cfg = &cfg->gpio_cfg;
		ret |= i2c_emul_register(i2c_dev, &data->gpio_data.emul);
		i2c_common_emul_init(&data->gpio_data);
	}

	ret |= ps8xxx_emul_tcpc_reset(emul);

	tcpci_emul_set_reg(emul, TCPC_REG_VENDOR_ID, PS8XXX_VENDOR_ID);
	tcpci_emul_set_reg(emul, TCPC_REG_PRODUCT_ID, data->prod_id);
	/* FW rev is never 0 in a working device. Set arbitrary FW rev. */
	tcpci_emul_set_reg(emul, PS8XXX_REG_FW_REV, 0x31);

	return ret;
}

#define PS8XXX_EMUL(n)                                                \
	static struct ps8xxx_emul_data ps8xxx_emul_data_##n = {		\
		.prod_id = PS8805_PRODUCT_ID,				\
		.p0_data = {						\
			.write_byte = ps8xxx_emul_p0_write_byte,	\
			.read_byte = ps8xxx_emul_p0_read_byte,		\
		},							\
		.p1_data = {						\
			.write_byte = ps8xxx_emul_p1_write_byte,	\
			.read_byte = ps8xxx_emul_p1_read_byte,		\
		},							\
		.gpio_data = {						\
			.write_byte = ps8xxx_emul_gpio_write_byte,	\
			.read_byte = ps8xxx_emul_gpio_read_byte,	\
		},							\
	};    \
                                                                      \
	static const struct ps8xxx_emul_cfg ps8xxx_emul_cfg_##n = {	\
		.p0_cfg = {						\
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &ps8xxx_emul_data_##n.p0_data,		\
			.addr = DT_INST_PROP(n, p0_i2c_addr),		\
		},							\
		.p1_cfg = {						\
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &ps8xxx_emul_data_##n.p1_data,		\
			.addr = DT_INST_PROP(n, p1_i2c_addr),		\
		},							\
		.gpio_cfg = {						\
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &ps8xxx_emul_data_##n.gpio_data,	\
			.addr = DT_INST_PROP(n, gpio_i2c_addr),		\
		},							\
	}; \
	TCPCI_EMUL_DEFINE(n, ps8xxx_emul_init, &ps8xxx_emul_cfg_##n,  \
			  &ps8xxx_emul_data_##n, &i2c_ps8xxx_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(PS8XXX_EMUL)

#ifdef CONFIG_ZTEST
#define PS8XXX_EMUL_RESET_RULE_BEFORE(n) \
	ps8xxx_emul_tcpc_reset(EMUL_DT_GET(DT_DRV_INST(n)));
static void ps8xxx_emul_reset_rule_before(const struct ztest_unit_test *test,
					  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(PS8XXX_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(PS8XXX_emul_reset, ps8xxx_emul_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
