/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_bb_retimer_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(emul_bb_retimer);

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#include "emul/emul_common_i2c.h"
#include "emul/emul_bb_retimer.h"

#include "driver/retimer/bb_retimer.h"

#define BB_DATA_FROM_I2C_EMUL(_emul)					     \
	CONTAINER_OF(CONTAINER_OF(_emul, struct i2c_common_emul_data, emul), \
		     struct bb_emul_data, common)

/** Run-time data used by the emulator */
struct bb_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Current state of all emulated BB retimer registers */
	uint32_t reg[BB_RETIMER_REG_COUNT];

	/** Vendor ID of emulated device */
	uint32_t vendor_id;

	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;

	/** Value of data dword in ongoing i2c message */
	uint32_t data_dword;
};

/** Check description in emul_bb_retimer.h */
void bb_emul_set_reg(struct i2c_emul *emul, int reg, uint32_t val)
{
	struct bb_emul_data *data;

	if (reg < 0 || reg > BB_RETIMER_REG_COUNT) {
		return;
	}

	data = BB_DATA_FROM_I2C_EMUL(emul);
	data->reg[reg] = val;
}

/** Check description in emul_bb_retimer.h */
uint32_t bb_emul_get_reg(struct i2c_emul *emul, int reg)
{
	struct bb_emul_data *data;

	if (reg < 0 || reg > BB_RETIMER_REG_COUNT) {
		return 0;
	}

	data = BB_DATA_FROM_I2C_EMUL(emul);

	return data->reg[reg];
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set)
{
	struct bb_emul_data *data;

	data = BB_DATA_FROM_I2C_EMUL(emul);
	data->error_on_ro_write = set;
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set)
{
	struct bb_emul_data *data;

	data = BB_DATA_FROM_I2C_EMUL(emul);
	data->error_on_rsvd_write = set;
}

/** Mask reserved bits in each register of BB retimer */
static const uint32_t bb_emul_rsvd_mask[] = {
	[BB_RETIMER_REG_VENDOR_ID]		= 0x00000000,
	[BB_RETIMER_REG_DEVICE_ID]		= 0x00000000,
	[0x02]					= 0xffffffff, /* Reserved */
	[0x03]					= 0xffffffff, /* Reserved */
	[BB_RETIMER_REG_CONNECTION_STATE]	= 0xc0201000,
	[BB_RETIMER_REG_TBT_CONTROL]		= 0xffffdfff,
	[0x06]					= 0xffffffff, /* Reserved */
	[BB_RETIMER_REG_EXT_CONNECTION_MODE]	= 0x08007f00,
};

/**
 * @brief Reset registers to default values
 *
 * @param emul Pointer to BB retimer emulator
 */
static void bb_emul_reset(struct i2c_emul *emul)
{
	struct bb_emul_data *data;

	data = BB_DATA_FROM_I2C_EMUL(emul);

	data->reg[BB_RETIMER_REG_VENDOR_ID]		= data->vendor_id;
	data->reg[BB_RETIMER_REG_DEVICE_ID]		= BB_RETIMER_DEVICE_ID;
	data->reg[0x02]					= 0x00; /* Reserved */
	data->reg[0x03]					= 0x00; /* Reserved */
	data->reg[BB_RETIMER_REG_CONNECTION_STATE]	= 0x00;
	data->reg[BB_RETIMER_REG_TBT_CONTROL]		= 0x00;
	data->reg[0x06]					= 0x00; /* Reserved */
	data->reg[BB_RETIMER_REG_EXT_CONNECTION_MODE]	= 0x00;
}

/**
 * @brief Handle I2C write message. It is checked if accessed register isn't RO
 *        and reserved bits are set to 0. Write set value of reg field of BB
 *        retimer emulator data ignoring reserved bits and write only bits.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bb_emul_handle_write(struct i2c_emul *emul, int reg, int msg_len)
{
	struct bb_emul_data *data;
	uint32_t val;

	data = BB_DATA_FROM_I2C_EMUL(emul);

	/* This write only selected register for I2C read message */
	if (msg_len < 2) {
		return 0;
	}

	val = data->data_dword;

	/*
	 * BB retimer ignores data bytes above 4 and use zeros if there is less
	 * then 4 data bytes. Emulator prints warning in that case.
	 */
	if (msg_len != 6) {
		LOG_WRN("Got %d bytes of WR data, expected 4", msg_len - 2);
	}

	if (reg <= BB_RETIMER_REG_DEVICE_ID ||
	    reg >= BB_RETIMER_REG_COUNT ||
	    reg == BB_RETIMER_REG_TBT_CONTROL) {
		if (data->error_on_ro_write) {
			LOG_ERR("Writing to reg 0x%x which is RO", reg);
			return -EIO;
		}

		return 0;
	}

	if (data->error_on_rsvd_write && bb_emul_rsvd_mask[reg] & val) {
		LOG_ERR("Writing 0x%x to reg 0x%x with rsvd bits mask 0x%x",
			val, reg, bb_emul_rsvd_mask[reg]);
		return -EIO;
	}

	/* Ignore all reserved bits */
	val &= ~bb_emul_rsvd_mask[reg];
	val |= data->reg[reg] & bb_emul_rsvd_mask[reg];

	data->reg[reg] = val;

	return 0;
}

/**
 * @brief Handle I2C read message. Response is obtained from reg field of bb
 *        emul data.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address to read
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bb_emul_handle_read(struct i2c_emul *emul, int reg)
{
	struct bb_emul_data *data;

	data = BB_DATA_FROM_I2C_EMUL(emul);

	if (reg >= BB_RETIMER_REG_COUNT) {
		LOG_ERR("Read unknown register 0x%x", reg);

		return -EIO;
	}

	data->data_dword = data->reg[reg];

	return 0;
}

/**
 * @brief Function called for each byte of write message. Data are stored
 *        in data_dword field of bb_emul_data
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 */
static int bb_emul_write_byte(struct i2c_emul *emul, int reg, uint8_t val,
			      int bytes)
{
	struct bb_emul_data *data;

	data = BB_DATA_FROM_I2C_EMUL(emul);

	if (bytes == 1) {
		data->data_dword = 0;
		if (val != 4) {
			LOG_WRN("Invalid write size");
		}
	} else if (bytes < 6) {
		data->data_dword |= val << (8 * (bytes - 2));
	}

	return 0;
}

/**
 * @brief Function called for each byte of read message. data_dword is converted
 *        to read message response.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readed
 *
 * @return 0 on success
 */
static int bb_emul_read_byte(struct i2c_emul *emul, int reg, uint8_t *val,
			     int bytes)
{
	struct bb_emul_data *data;

	data = BB_DATA_FROM_I2C_EMUL(emul);

	*val = data->data_dword & 0xff;
	data->data_dword >>= 8;

	return 0;
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        register.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int bb_emul_access_reg(struct i2c_emul *emul, int reg, int bytes,
			      bool read)
{
	return reg;
}

/* Device instantiation */

static struct i2c_emul_api bb_emul_api = {
	.transfer = i2c_common_emul_transfer,
};

/**
 * @brief Set up a new BB retimer emulator
 *
 * This should be called for each BB retimer device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int bb_emul_init(const struct emul *emul,
			const struct device *parent)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;
	struct i2c_common_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &bb_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;
	i2c_common_emul_init(data);

	ret = i2c_emul_register(parent, emul->dev_label, &data->emul);

	bb_emul_reset(&data->emul);

	return ret;
}

#define BB_RETIMER_EMUL(n)						\
	static struct bb_emul_data bb_emul_data_##n = {			\
		.vendor_id = DT_ENUM_TOKEN(DT_DRV_INST(n), vendor),	\
		.error_on_ro_write = DT_INST_PROP(n, error_on_ro_write),\
		.error_on_rsvd_write = DT_INST_PROP(n,			\
					error_on_reserved_bit_write),	\
		.common = {						\
			.start_write = NULL,				\
			.write_byte = bb_emul_write_byte,		\
			.finish_write = bb_emul_handle_write,		\
			.start_read = bb_emul_handle_read,		\
			.read_byte = bb_emul_read_byte,			\
			.finish_read = NULL,				\
			.access_reg = bb_emul_access_reg,		\
		},							\
	};								\
									\
	static const struct i2c_common_emul_cfg bb_emul_cfg_##n = {	\
		.i2c_label = DT_INST_BUS_LABEL(n),			\
		.data = &bb_emul_data_##n.common,			\
		.addr = DT_INST_REG_ADDR(n),				\
	};								\
	EMUL_DEFINE(bb_emul_init, DT_DRV_INST(n), &bb_emul_cfg_##n)

DT_INST_FOREACH_STATUS_OKAY(BB_RETIMER_EMUL)

#define BB_RETIMER_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &bb_emul_data_##n.common.emul;

/** Check description in emul_bb_emulator.h */
struct i2c_emul *bb_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(BB_RETIMER_EMUL_CASE)

	default:
		return NULL;
	}
}
