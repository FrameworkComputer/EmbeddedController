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

#include "emul/emul_bb_retimer.h"

#include "driver/retimer/bb_retimer.h"

/**
 * Describe if there is no ongoing I2C message or if there is message handled
 * at the moment (last message doesn't ended with stop or write is not followed
 * by read).
 */
enum bb_emul_msg_state {
	BB_EMUL_NONE_MSG,
	BB_EMUL_IN_WRITE,
	BB_EMUL_IN_READ
};

/** Run-time data used by the emulator */
struct bb_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** BB retimer device being emulated */
	const struct device *i2c;
	/** Configuration information */
	const struct bb_emul_cfg *cfg;

	/** Current state of all emulated BB retimer registers */
	uint32_t reg[BB_RETIMER_REG_COUNT];

	/** Vendor ID of emulated device */
	uint32_t vendor_id;

	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;

	/** Current state of I2C bus (if emulator is handling message) */
	enum bb_emul_msg_state msg_state;
	/** Number of already handled bytes in ongoing message */
	int msg_byte;
	/** Register selected in last write command */
	uint8_t cur_reg;
	/** Value of data dword in ongoing i2c message */
	uint32_t data_dword;

	/** Custom write function called on I2C write opperation */
	bb_emul_write_func write_func;
	/** Data passed to custom write function */
	void *write_func_data;
	/** Custom read function called on I2C read opperation */
	bb_emul_read_func read_func;
	/** Data passed to custom read function */
	void *read_func_data;

	/** Control if read should fail on given register */
	int read_fail_reg;
	/** Control if write should fail on given register */
	int write_fail_reg;

	/** Mutex used to control access to emulator data */
	struct k_mutex data_mtx;
};

/** Static configuration for the emulator */
struct bb_emul_cfg {
	/** Label of the I2C bus this emulator connects to */
	const char *i2c_label;
	/** Pointer to run-time data */
	struct bb_emul_data *data;
	/** Address of BB retimer on i2c bus */
	uint16_t addr;
};

/** Check description in emul_bb_retimer.h */
int bb_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout)
{
	struct bb_emul_data *data;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);

	return k_mutex_lock(&data->data_mtx, timeout);
}

/** Check description in emul_bb_retimer.h */
int bb_emul_unlock_data(struct i2c_emul *emul)
{
	struct bb_emul_data *data;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);

	return k_mutex_unlock(&data->data_mtx);
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_write_func(struct i2c_emul *emul,
			    bb_emul_write_func func, void *data)
{
	struct bb_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	emul_data->write_func = func;
	emul_data->write_func_data = data;
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_read_func(struct i2c_emul *emul,
			   bb_emul_read_func func, void *data)
{
	struct bb_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	emul_data->read_func = func;
	emul_data->read_func_data = data;
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_reg(struct i2c_emul *emul, int reg, uint32_t val)
{
	struct bb_emul_data *data;

	if (reg < 0 || reg > BB_RETIMER_REG_COUNT) {
		return;
	}

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	data->reg[reg] = val;
}

/** Check description in emul_bb_retimer.h */
uint32_t bb_emul_get_reg(struct i2c_emul *emul, int reg)
{
	struct bb_emul_data *data;

	if (reg < 0 || reg > BB_RETIMER_REG_COUNT) {
		return 0;
	}

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);

	return data->reg[reg];
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_read_fail_reg(struct i2c_emul *emul, int reg)
{
	struct bb_emul_data *data;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	data->read_fail_reg = reg;
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_write_fail_reg(struct i2c_emul *emul, int reg)
{
	struct bb_emul_data *data;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	data->write_fail_reg = reg;
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set)
{
	struct bb_emul_data *data;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	data->error_on_ro_write = set;
}

/** Check description in emul_bb_retimer.h */
void bb_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set)
{
	struct bb_emul_data *data;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);
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

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);

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
 *        retimer emulator data ignoring reserved bits and write only bits. Some
 *        commands are handled specialy. Before any handling, custom function
 *        is called if provided.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register which is written
 * @param val Value being written to @p reg
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bb_emul_handle_write(struct i2c_emul *emul, int reg, uint32_t val,
				int msg_len)
{
	struct bb_emul_data *data;
	int ret;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);

	/*
	 * BB retimer ignores data bytes above 4 and use zeros if there is less
	 * then 4 data bytes. Emulator prints warning in that case.
	 */
	if (msg_len != 6) {
		LOG_WRN("Got %d bytes of WR data, expected 4", msg_len - 2);
	}

	if (data->write_func) {
		ret = data->write_func(emul, reg, val, data->write_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			return 0;
		}
	}

	if (data->write_fail_reg == reg ||
	    data->write_fail_reg == BB_EMUL_FAIL_ALL_REG) {
		return -EIO;
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
 *        emul data. When accessing accelerometer value, register data is first
 *        computed using internal emulator state. Before default handler, custom
 *        user read function is called if provided.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address to read
 * @param buf Pointer where result should be stored
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bb_emul_handle_read(struct i2c_emul *emul, int reg, uint32_t *buf)
{
	struct bb_emul_data *data;
	int ret;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);

	if (data->read_func) {
		ret = data->read_func(emul, reg, data->read_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			/* Immediately return value set by custom function */
			*buf = data->reg[reg];

			return 0;
		}
	}

	if (data->read_fail_reg == reg ||
	    data->read_fail_reg == BB_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	if (reg >= BB_RETIMER_REG_COUNT) {
		LOG_ERR("Read unknown register 0x%x", reg);

		return -EIO;
	}

	*buf = data->reg[reg];

	return 0;
}

/**
 * @biref Emulate an I2C transfer to a BB retimer
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
static int bb_emul_transfer(struct i2c_emul *emul, struct i2c_msg *msgs,
			    int num_msgs, int addr)
{
	const struct bb_emul_cfg *cfg;
	struct bb_emul_data *data;
	int ret, i;
	bool read;

	data = CONTAINER_OF(emul, struct bb_emul_data, emul);
	cfg = data->cfg;

	if (cfg->addr != addr) {
		LOG_ERR("Address mismatch, expected %02x, got %02x", cfg->addr,
			addr);
		return -EIO;
	}

	i2c_dump_msgs("emul", msgs, num_msgs, addr);

	for (; num_msgs > 0; num_msgs--, msgs++) {
		read = msgs->flags & I2C_MSG_READ;

		switch (data->msg_state) {
		case BB_EMUL_NONE_MSG:
			data->data_dword = 0;
			data->msg_byte = 0;
			break;
		case BB_EMUL_IN_WRITE:
			if (read) {
				/* Finish write command */
				if (data->msg_byte >= 2) {
					k_mutex_lock(&data->data_mtx,
						     K_FOREVER);
					ret = bb_emul_handle_write(emul,
							data->cur_reg,
							data->data_dword,
							data->msg_byte);
					k_mutex_unlock(&data->data_mtx);
					if (ret) {
						return -EIO;
					}
				}
				data->data_dword = 0;
				data->msg_byte = 0;
			}
			break;
		case BB_EMUL_IN_READ:
			if (!read) {
				data->data_dword = 0;
				data->msg_byte = 0;
			}
			break;
		}
		data->msg_state = read ? BB_EMUL_IN_READ : BB_EMUL_IN_WRITE;

		if (msgs->flags & I2C_MSG_STOP) {
			data->msg_state = BB_EMUL_NONE_MSG;
		}

		if (!read) {
			/* Dispatch wrtie command */
			for (i = 0; i < msgs->len; i++) {
				switch (data->msg_byte) {
				case 0:
					data->cur_reg = msgs->buf[i];
					break;
				case 1:
					/*
					 * BB retimer ignores size, but it
					 * should be 4, so emulator check this.
					 */
					if (msgs->buf[i] != 4) {
						LOG_WRN("Invalid write size");
					}
					break;
				default:
					data->data_dword |=
						(msgs->buf[i] & 0xff) <<
						(8 * (data->msg_byte - 2));
				}
				data->msg_byte++;
			}

			/* Execute write command */
			if (msgs->flags & I2C_MSG_STOP && data->msg_byte >= 2) {
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = bb_emul_handle_write(emul, data->cur_reg,
							   data->data_dword,
							   data->msg_byte);
				k_mutex_unlock(&data->data_mtx);
				if (ret) {
					return -EIO;
				}
			}
		} else {
			/* Prepare response */
			if (data->msg_byte == 0) {
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = bb_emul_handle_read(emul, data->cur_reg,
							  &data->data_dword);
				k_mutex_unlock(&data->data_mtx);
				if (ret) {
					return -EIO;
				}
			}

			for (i = 0; i < msgs->len; i++) {
				msgs->buf[i] = data->data_dword & 0xff;
				data->data_dword >>= 8;

				data->msg_byte++;
			}
		}
	}

	return 0;
}

/* Device instantiation */

static struct i2c_emul_api bb_emul_api = {
	.transfer = bb_emul_transfer,
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
	const struct bb_emul_cfg *cfg = emul->cfg;
	struct bb_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &bb_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;
	k_mutex_init(&data->data_mtx);

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
		.msg_state = BB_EMUL_NONE_MSG,				\
		.cur_reg = 0,						\
		.write_func = NULL,					\
		.read_func = NULL,					\
		.write_fail_reg = BB_EMUL_NO_FAIL_REG,			\
		.read_fail_reg = BB_EMUL_NO_FAIL_REG,			\
	};								\
									\
	static const struct bb_emul_cfg bb_emul_cfg_##n = {		\
		.i2c_label = DT_INST_BUS_LABEL(n),			\
		.data = &bb_emul_data_##n,				\
		.addr = DT_INST_REG_ADDR(n),				\
	};								\
	EMUL_DEFINE(bb_emul_init, DT_DRV_INST(n), &bb_emul_cfg_##n)

DT_INST_FOREACH_STATUS_OKAY(BB_RETIMER_EMUL)

#define BB_RETIMER_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &bb_emul_data_##n.emul;

/** Check description in emul_bb_emulator.h */
struct i2c_emul *bb_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(BB_RETIMER_EMUL_CASE)

	default:
		return NULL;
	}
}
