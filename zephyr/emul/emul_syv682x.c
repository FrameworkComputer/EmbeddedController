/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT zephyr_syv682x_emul

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(syv682x);
#include <stdint.h>
#include <string.h>

#include "emul/emul_syv682x.h"

#define EMUL_REG_COUNT (SYV682X_CONTROL_4_REG + 1)
#define EMUL_REG_IS_VALID(reg) (reg >= 0 && reg < EMUL_REG_COUNT)

struct syv682x_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** Smart battery device being emulated */
	const struct device *i2c;
	/** Configuration information */
	const struct syv682x_emul_cfg *cfg;
	/** Current state of all emulated SYV682x registers */
	uint8_t reg[EMUL_REG_COUNT];
	/**
	 * Current state of conditions affecting interrupt bits, as distinct
	 * from the current values of those bits stored in reg.
	 */
	uint8_t status_cond;
	uint8_t control_4_cond;
};

/** Static configuration for the emulator */
struct syv682x_emul_cfg {
	/** Label of the I2C bus this emulator connects to */
	const char *i2c_label;
	/** Address of smart battery on i2c bus */
	uint16_t addr;
	/** Pointer to runtime data */
	struct syv682x_emul_data *data;
};

int syv682x_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct syv682x_emul_data *data;

	if (!EMUL_REG_IS_VALID(reg))
		return -EIO;

	data = CONTAINER_OF(emul, struct syv682x_emul_data, emul);
	data->reg[reg] = val;

	return 0;
}

void syv682x_emul_set_status(struct i2c_emul *emul, uint8_t val)
{
	struct syv682x_emul_data *data;

	data = CONTAINER_OF(emul, struct syv682x_emul_data, emul);
	data->status_cond = val;
	data->reg[SYV682X_STATUS_REG] |= val;

	if (val & (SYV682X_STATUS_TSD | SYV682X_STATUS_OVP |
				SYV682X_STATUS_OC_HV)) {
		data->reg[SYV682X_CONTROL_1_REG] |= SYV682X_CONTROL_1_PWR_ENB;
	}

	/*
	 * TODO(b/190519131): Make this emulator trigger GPIO-based interrupts
	 * by itself based on the status. In real life, the device should turn
	 * the power path off when either of these conditions occurs, and they
	 * should quickly dissipate. If they somehow stay set, the device should
	 * interrupt continuously.
	 */
}

int syv682x_emul_get_reg(struct i2c_emul *emul, int reg, uint8_t *val)
{
	struct syv682x_emul_data *data;

	if (!EMUL_REG_IS_VALID(reg))
		return -EIO;

	data = CONTAINER_OF(emul, struct syv682x_emul_data, emul);
	*val = data->reg[reg];

	return 0;
}

/**
 * Emulate an I2C transfer to an SYV682x. This handles simple reads and writes.
 *
 * @param emul I2C emulation information
 * @param msgs List of messages to process. For 'read' messages, this function
 *             updates the 'buf' member with the data that was read
 * @param num_msgs Number of messages to process
 * @param addr Address of the I2C target device.
 *
 * @return 0 on success, -EIO on general input / output error
 */
static int syv682x_emul_transfer(struct i2c_emul *emul, struct i2c_msg *msgs,
			      int num_msgs, int addr)
{
	const struct syv682x_emul_cfg *cfg;
	struct syv682x_emul_data *data;
	data = CONTAINER_OF(emul, struct syv682x_emul_data, emul);
	cfg = data->cfg;

	if (cfg->addr != addr) {
		LOG_ERR("Address mismatch, expected %02x, got %02x", cfg->addr,
				addr);
		return -EIO;
	}

	i2c_dump_msgs("emul", msgs, num_msgs, addr);

	if (num_msgs == 1) {
		int reg = msgs[0].buf[0];
		uint8_t val = msgs[0].buf[1];

		if (!((msgs[0].flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE
					&& msgs[0].len == 2)) {
			LOG_ERR("Unexpected write msgs");
			return -EIO;
		}

		switch (reg) {
		case SYV682X_CONTROL_1_REG:
			/*
			 * If OVP or TSD is active, the power path stays
			 * disabled.
			 */
			if (data->status_cond & (SYV682X_STATUS_TSD |
						SYV682X_STATUS_OVP))
				val |= SYV682X_CONTROL_1_PWR_ENB;
			break;
		default:
			break;
		}

		return syv682x_emul_set_reg(emul, msgs[0].buf[0], val);
	} else if (num_msgs == 2) {
		int ret;
		int reg;
		uint8_t *buf;

		if (!((msgs[0].flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE
					&& msgs[0].len == 1
					&& (msgs[1].flags & I2C_MSG_RW_MASK) ==
						I2C_MSG_READ
					&& (msgs[1].len == 1))) {
			LOG_ERR("Unexpected read msgs");
			return -EIO;
		}

		reg = msgs[0].buf[0];
		buf = &msgs[1].buf[0];
		ret = syv682x_emul_get_reg(emul, reg, buf);

		switch (reg) {
		/*
		 * These registers are clear-on-read (if the underlying
		 * condition has cleared).
		 */
		case SYV682X_STATUS_REG:
			syv682x_emul_set_reg(emul, reg, data->status_cond);
			break;
		case SYV682X_CONTROL_4_REG:
			syv682x_emul_set_reg(emul, reg, data->control_4_cond);
			break;
		default:
			break;
		}

		return ret;
	} else {
		LOG_ERR("Unexpected num_msgs");
		return -EIO;
	}
}

/* Device instantiation */

static struct i2c_emul_api syv682x_emul_api = {
	.transfer = syv682x_emul_transfer,
};

/**
 * @brief Set up a new SYV682x emulator
 *
 * This should be called for each SYV682x device that needs to be emulated. It
 * registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int syv682x_emul_init(const struct emul *emul,
			  const struct device *parent)
{
	const struct syv682x_emul_cfg *cfg = emul->cfg;
	struct syv682x_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &syv682x_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;
	memset(data->reg, 0, sizeof(data->reg));

	ret = i2c_emul_register(parent, emul->dev_label, &data->emul);

	return ret;
}

#define SYV682X_EMUL(n)                                                       \
	static struct syv682x_emul_data syv682x_emul_data_##n = {};           \
	static const struct syv682x_emul_cfg syv682x_emul_cfg_##n = {         \
		.i2c_label = DT_INST_BUS_LABEL(n),                            \
		.data = &syv682x_emul_data_##n,                               \
		.addr = DT_INST_REG_ADDR(n),                                  \
	};                                                                    \
	EMUL_DEFINE(syv682x_emul_init, DT_DRV_INST(n), &syv682x_emul_cfg_##n, \
		    &syv682x_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(SYV682X_EMUL)

#define SYV682X_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &syv682x_emul_data_##n.emul;


struct i2c_emul *syv682x_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(SYV682X_EMUL_CASE)

	default:
		return NULL;
	}
}
