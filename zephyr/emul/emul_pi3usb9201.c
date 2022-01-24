/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT zephyr_pi3usb9201_emul

#include <device.h>
#include <drivers/emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#include "emul/emul_pi3usb9201.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(emul_pi3usb9201, LOG_LEVEL_DBG);

#define EMUL_REG_COUNT (PI3USB9201_REG_HOST_STS + 1)
#define EMUL_REG_IS_VALID(reg) (reg >= 0 && reg < EMUL_REG_COUNT)

/** Run-time data used by the emulator */
struct pi3usb9201_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** pi3usb9201 device being emulated */
	const struct device *i2c;
	/** Configuration information */
	const struct pi3usb9201_emul_cfg *cfg;
	/** Current state of all emulated pi3usb9201 registers */
	uint8_t reg[EMUL_REG_COUNT];
};

/** Static configuration for the emulator */
struct pi3usb9201_emul_cfg {
	/** Label of the I2C bus this emulator connects to */
	const char *i2c_label;
	/** Pointer to run-time data */
	struct pi3usb9201_emul_data *data;
	/** Address of pi3usb9201 on i2c bus */
	uint16_t addr;
};

int pi3usb9201_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct pi3usb9201_emul_data *data;

	if (!EMUL_REG_IS_VALID(reg))
		return -EIO;

	data = CONTAINER_OF(emul, struct pi3usb9201_emul_data, emul);
	data->reg[reg] = val;

	return 0;
}

int pi3usb9201_emul_get_reg(struct i2c_emul *emul, int reg, uint8_t *val)
{
	struct pi3usb9201_emul_data *data;

	if (!EMUL_REG_IS_VALID(reg))
		return -EIO;

	data = CONTAINER_OF(emul, struct pi3usb9201_emul_data, emul);
	*val = data->reg[reg];

	return 0;
}

static void pi3usb9201_emul_reset(struct i2c_emul *emul)
{
	struct pi3usb9201_emul_data *data;

	data = CONTAINER_OF(emul, struct pi3usb9201_emul_data, emul);

	data->reg[PI3USB9201_REG_CTRL_1]     = 0;
	data->reg[PI3USB9201_REG_CTRL_2]     = 0;
	data->reg[PI3USB9201_REG_CLIENT_STS] = 0;
	data->reg[PI3USB9201_REG_HOST_STS]   = 0;
}

/**
 * Emulate an I2C transfer to a pi3usb9201
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
static int pi3usb9201_emul_transfer(struct i2c_emul *emul, struct i2c_msg *msgs,
				    int num_msgs, int addr)
{
	const struct pi3usb9201_emul_cfg *cfg;
	struct pi3usb9201_emul_data *data;

	data = CONTAINER_OF(emul, struct pi3usb9201_emul_data, emul);
	cfg = data->cfg;

	if (cfg->addr != addr) {
		LOG_ERR("Address mismatch, expected %02x, got %02x", cfg->addr,
			addr);
		return -EIO;
	}

	i2c_dump_msgs("emul", msgs, num_msgs, addr);

	if (num_msgs == 1) {
		if (!(((msgs[0].flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE)
		      && (msgs[0].len == 2))) {
			LOG_ERR("Unexpected write msgs");
			return -EIO;
		}
		return pi3usb9201_emul_set_reg(emul, msgs[0].buf[0],
					       msgs[0].buf[1]);
	} else if (num_msgs == 2) {
		if (!(((msgs[0].flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE)
		      && (msgs[0].len == 1)
		      && ((msgs[1].flags & I2C_MSG_RW_MASK) == I2C_MSG_READ)
		      && (msgs[1].len == 1))) {
			LOG_ERR("Unexpected read msgs");
			return -EIO;
		}
		return pi3usb9201_emul_get_reg(emul, msgs[0].buf[0],
					       &(msgs[1].buf[0]));
	} else {
		LOG_ERR("Unexpected num_msgs");
		return -EIO;
	}

}

/* Device instantiation */

static struct i2c_emul_api pi3usb9201_emul_api = {
	.transfer = pi3usb9201_emul_transfer,
};

/**
 * @brief Set up a new pi3usb9201 emulator
 *
 * This should be called for each pi3usb9201 device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int pi3usb9201_emul_init(const struct emul *emul,
			 const struct device *parent)
{
	const struct pi3usb9201_emul_cfg *cfg = emul->cfg;
	struct pi3usb9201_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &pi3usb9201_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;

	ret = i2c_emul_register(parent, emul->dev_label, &data->emul);

	pi3usb9201_emul_reset(&data->emul);

	return ret;
}

#define PI3USB9201_EMUL(n)                                                  \
	static struct pi3usb9201_emul_data pi3usb9201_emul_data_##n = {};   \
	static const struct pi3usb9201_emul_cfg pi3usb9201_emul_cfg_##n = { \
		.i2c_label = DT_INST_BUS_LABEL(n),                          \
		.data = &pi3usb9201_emul_data_##n,                          \
		.addr = DT_INST_REG_ADDR(n),                                \
	};                                                                  \
	EMUL_DEFINE(pi3usb9201_emul_init, DT_DRV_INST(n),                   \
		    &pi3usb9201_emul_cfg_##n, &pi3usb9201_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(PI3USB9201_EMUL)

#define PI3USB9201_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &pi3usb9201_emul_data_##n.emul;

struct i2c_emul *pi3usb9201_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(PI3USB9201_EMUL_CASE)

	default:
		return NULL;
	}
}
