/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/anx7452.h"
#include "emul/emul_anx7452.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT analogix_anx7452

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_anx7452);

/** Run-time data used by the emulator */
struct anx7452_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data top_data;
	struct i2c_common_emul_data ctltop_data;

	/** Configuration information */
	const struct anx7452_emul_cfg *cfg;

	/** Current state of all emulated ANX7452 retimer registers */
	uint8_t top_reg;

	uint8_t ctltop_cfg0_reg;

	uint8_t ctltop_cfg1_reg;

	uint8_t ctltop_cfg2_reg;
};

/** Constant configuration of the emulator */
struct anx7452_emul_cfg {
	const struct i2c_common_emul_cfg top_cfg;
	const struct i2c_common_emul_cfg ctltop_cfg;
};

/* Workhorse for mapping i2c reg to internal emulator data access */
static uint8_t *anx7452_emul_get_reg_ptr(struct anx7452_emul_data *data,
					 int reg)
{
	switch (reg) {
	case ANX7452_TOP_STATUS_REG:
		return &(data->top_reg);
	case ANX7452_CTLTOP_CFG0_REG:
		return &(data->ctltop_cfg0_reg);
	case ANX7452_CTLTOP_CFG1_REG:
		return &(data->ctltop_cfg1_reg);
	case ANX7452_CTLTOP_CFG2_REG:
		return &(data->ctltop_cfg2_reg);
	default:
		__ASSERT(false, "Unimplemented Register Access Error on 0x%x",
			 reg);
		/* Statement never reached, required for compiler warnings */
		return NULL;
	}
}

/** Check description in emul_anx7452.h */
void anx7452_emul_set_reg(const struct emul *emul, int reg, uint8_t val)
{
	struct anx7452_emul_data *data = emul->data;

	uint8_t *reg_to_write = anx7452_emul_get_reg_ptr(data, reg);
	*reg_to_write = val;
}

/** Check description in emul_anx7452.h */
uint8_t anx7452_emul_get_reg(const struct emul *emul, int reg)
{
	struct anx7452_emul_data *data = emul->data;
	uint8_t *reg_to_read = anx7452_emul_get_reg_ptr(data, reg);

	return *reg_to_read;
}

/** Check description in emul_anx7452.h */
void anx7452_emul_reset(const struct emul *emul)
{
	struct anx7452_emul_data *data;

	data = emul->data;

	data->top_reg = 0x01 | ANX7452_TOP_RESERVED_BIT;

	data->ctltop_cfg0_reg = 0x00;

	data->ctltop_cfg1_reg = 0x00;

	data->ctltop_cfg2_reg = 0x00;
}

static int anx7452_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	struct anx7452_emul_data *data = emul->data;

	uint8_t *reg_to_write = anx7452_emul_get_reg_ptr(data, reg);
	*reg_to_write = val;

	return 0;
}

static int anx7452_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int bytes)
{
	struct anx7452_emul_data *data = emul->data;
	uint8_t *reg_to_read = anx7452_emul_get_reg_ptr(data, reg);

	*val = *reg_to_read;

	return 0;
}

/**
 * Emulate an I2C transfer for ANX7452
 *
 * This handles simple reads and writes
 *
 * @param emul I2C emulation information
 * @param msgs List of messages to process
 * @param num_msgs Number of messages to process
 * @param addr Address of the I2C target device
 *
 * @retval 0 If successful
 * @retval -EIO General input / output error
 */
static int anx7452_emul_transfer(const struct emul *emul, struct i2c_msg *msgs,
				 int num_msgs, int addr)
{
	const struct anx7452_emul_cfg *cfg;
	struct anx7452_emul_data *data;
	struct i2c_common_emul_data *common_data;

	data = emul->data;
	cfg = emul->cfg;

	if (addr == cfg->top_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg = &cfg->top_cfg;

		common_data = &data->top_data;
		return i2c_common_emul_transfer_workhorse(
			emul, common_data, common_cfg, msgs, num_msgs, addr);
	} else if (addr == cfg->ctltop_cfg.addr) {
		const struct i2c_common_emul_cfg *common_cfg = &cfg->ctltop_cfg;

		common_data = &data->ctltop_data;
		return i2c_common_emul_transfer_workhorse(
			emul, common_data, common_cfg, msgs, num_msgs, addr);
	}

	LOG_ERR("Cannot map address %02x", addr);
	return -EIO;
}

/* Device instantiation */

static struct i2c_emul_api anx7452_emul_api = {
	.transfer = anx7452_emul_transfer,
};

/* Device instantiation */

/**
 * @brief Set up a new ANX7452 retimer emulator
 *
 * This should be called for each ANX7452 retimer device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int anx7452_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	const struct anx7452_emul_cfg *cfg;
	struct anx7452_emul_data *data;
	int ret = 0;

	data = emul->data;
	cfg = emul->cfg;

	data->top_data.emul.api = &anx7452_emul_api;
	data->top_data.emul.addr = cfg->top_cfg.addr;
	data->top_data.emul.target = emul;
	data->top_data.i2c = parent;
	data->top_data.cfg = &cfg->top_cfg;
	i2c_common_emul_init(&data->top_data);

	data->ctltop_data.emul.api = &anx7452_emul_api;
	data->ctltop_data.emul.addr = cfg->ctltop_cfg.addr;
	data->ctltop_data.emul.target = emul;
	data->ctltop_data.i2c = parent;
	data->ctltop_data.cfg = &cfg->ctltop_cfg;
	i2c_common_emul_init(&data->ctltop_data);

	ret |= i2c_emul_register(parent, &data->top_data.emul);
	ret |= i2c_emul_register(parent, &data->ctltop_data.emul);

	anx7452_emul_reset(emul);

	return ret;
}

#define ANX7452_EMUL(n)                                                   \
	static struct anx7452_emul_data anx7452_emul_data_##n = {	  \
		.top_data = {						  \
			.write_byte = anx7452_emul_write_byte,		  \
			.read_byte = anx7452_emul_read_byte,		  \
		},                                                        \
		.ctltop_data = {					  \
			.write_byte = anx7452_emul_write_byte,		  \
			.read_byte = anx7452_emul_read_byte,		  \
		},							  \
	};     \
	static const struct anx7452_emul_cfg anx7452_emul_cfg_##n = {	\
		.top_cfg = {						\
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &anx7452_emul_data_##n.top_data,	\
			.addr = DT_INST_REG_ADDR(n),		\
		},							\
		.ctltop_cfg = {						\
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &anx7452_emul_data_##n.ctltop_data,	\
			.addr = ANX7452_I2C_ADDR_CTLTOP_FLAGS,		\
		},		                                        \
	};   \
	EMUL_DT_INST_DEFINE(n, anx7452_emul_init, &anx7452_emul_data_##n, \
			    &anx7452_emul_cfg_##n, &anx7452_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(ANX7452_EMUL);

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_anx7452_get_i2c_common_data(const struct emul *emul,
				 enum anx7452_emul_port port)
{
	struct anx7452_emul_data *data;

	data = emul->data;

	switch (port) {
	case TOP_EMUL_PORT:
		return &data->top_data;
	case CTLTOP_EMUL_PORT:
		return &data->ctltop_data;
	default:
		return NULL;
	}
}
