/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_common_i2c);

/** Check description in emul_common_i2c.h */
int i2c_common_emul_lock_data(struct i2c_common_emul_data *common_data,
			      k_timeout_t timeout)
{
	return k_mutex_lock(&common_data->data_mtx, timeout);
}

/** Check description in emul_common_i2c.h */
int i2c_common_emul_unlock_data(struct i2c_common_emul_data *common_data)
{
	return k_mutex_unlock(&common_data->data_mtx);
}

/** Check description in emul_common_i2c.h */
void i2c_common_emul_set_write_func(struct i2c_common_emul_data *common_data,
				    i2c_common_emul_write_func func, void *data)
{
	common_data->write_func = func;
	common_data->write_func_data = data;
}

/** Check description in emul_common_i2c.h */
void i2c_common_emul_set_read_func(struct i2c_common_emul_data *common_data,
				   i2c_common_emul_read_func func, void *data)
{
	common_data->read_func = func;
	common_data->read_func_data = data;
}

/** Check description in emul_common_i2c.h */
void i2c_common_emul_set_read_fail_reg(struct i2c_common_emul_data *common_data,
				       int reg)
{
	common_data->read_fail_reg = reg;
}

/** Check description in emul_common_i2c.h */
void i2c_common_emul_set_write_fail_reg(
	struct i2c_common_emul_data *common_data, int reg)
{
	common_data->write_fail_reg = reg;
}

/**
 * @brief Call start_write emulator callback if set. It handles first byte
 *        of I2C write message.
 *
 * @param emul Pointer to emulator
 * @param data Pointer to emulator data
 *
 * @retval start_write emulator callback return code
 */
static int i2c_common_emul_start_write(const struct emul *target,
				       struct i2c_common_emul_data *data)
{
	int ret = 0;

	data->msg_byte = 0;

	if (data->start_write) {
		k_mutex_lock(&data->data_mtx, K_FOREVER);
		ret = data->start_write(target, data->cur_reg);
		k_mutex_unlock(&data->data_mtx);
	}

	return ret;
}

/**
 * @brief Call finish_write emulator callback if set. It is called after last
 *        byte of I2C write message.
 *
 * @param emul Pointer to emulator
 * @param data Pointer to emulator data
 *
 * @retval finish_write emulator callback return code
 */
static int i2c_common_emul_finish_write(const struct emul *target,
					struct i2c_common_emul_data *data)
{
	int ret = 0;

	if (data->finish_write) {
		k_mutex_lock(&data->data_mtx, K_FOREVER);
		ret = data->finish_write(target, data->cur_reg, data->msg_byte);
		k_mutex_unlock(&data->data_mtx);
	}

	return ret;
}

/**
 * @brief Call start_read emulator callback if set. It prepares emulator at
 *        the beginning of I2C read message.
 *
 * @param emul Pointer to emulator
 * @param data Pointer to emulator data
 *
 * @retval start_read emulator callback return code
 */
static int i2c_common_emul_start_read(const struct emul *target,
				      struct i2c_common_emul_data *data)
{
	int ret = 0;

	data->msg_byte = 0;

	if (data->start_read) {
		k_mutex_lock(&data->data_mtx, K_FOREVER);
		ret = data->start_read(target, data->cur_reg);
		k_mutex_unlock(&data->data_mtx);
	}

	return ret;
}

/**
 * @brief Call finish_read emulator callback if set. It is called after last
 *        byte of I2C read message.
 *
 * @param emul Pointer to emulator
 * @param data Pointer to emulator data
 *
 * @retval finish_read emulator callback return code
 */
static int i2c_common_emul_finish_read(const struct emul *target,
				       struct i2c_common_emul_data *data)
{
	int ret = 0;

	if (data->finish_read) {
		k_mutex_lock(&data->data_mtx, K_FOREVER);
		ret = data->finish_read(target, data->cur_reg, data->msg_byte);
		k_mutex_unlock(&data->data_mtx);
	}

	return ret;
}

/**
 * @brief Handle byte from I2C write message. First custom user handler is
 *        called (if set). Next accessed register is compared with selected
 *        by user fail register. Lastly, specific I2C device emulator handler
 *        is called.
 *
 * @param emul Pointer to emulator
 * @param data Pointer to emulator data
 * @param val Value of current byte
 *
 * @retval 0 If successful
 * @retval -EIO General input / output error
 */
static int i2c_common_emul_write_byte(const struct emul *target,
				      struct i2c_common_emul_data *data,
				      uint8_t val)
{
	int reg, ret;

	/* Check if user wants to fail on accessed register */
	if (data->access_reg) {
		reg = data->access_reg(target, data->cur_reg, data->msg_byte,
				       false /* = read */);
	} else {
		/* Ignore first (register address) byte */
		reg = data->cur_reg + data->msg_byte - 1;
	}

	if (data->write_fail_reg == reg ||
	    data->write_fail_reg == I2C_COMMON_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	/* Custom user handler */
	if (data->write_func) {
		ret = data->write_func(target, data->cur_reg, val,
				       data->msg_byte, data->write_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			return 0;
		}
	}

	/* Emulator handler */
	if (data->write_byte) {
		k_mutex_lock(&data->data_mtx, K_FOREVER);
		ret = data->write_byte(target, data->cur_reg, val,
				       data->msg_byte);
		k_mutex_unlock(&data->data_mtx);
		if (ret) {
			return -EIO;
		}
	}

	return 0;
}

/**
 * @brief Handle byte from I2C read message. First custom user handler is
 *        called (if set). Next accessed register is compared with selected
 *        by user fail register. Lastly, specific I2C device emulator handler
 *        is called.
 *
 * @param emul Pointer to emulator
 * @param data Pointer to emulator data
 * @param val Pointer to buffer where current response byte should be stored
 *
 * @retval 0 If successful
 * @retval -EIO General input / output error
 */
static int i2c_common_emul_read_byte(const struct emul *target,
				     struct i2c_common_emul_data *data,
				     uint8_t *val)
{
	int reg, ret;

	/* Check if user wants to fail on accessed register */
	if (data->access_reg) {
		reg = data->access_reg(target, data->cur_reg, data->msg_byte,
				       true /* = read */);
	} else {
		reg = data->cur_reg + data->msg_byte;
	}

	if (data->read_fail_reg == reg ||
	    data->read_fail_reg == I2C_COMMON_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	/* Custom user handler */
	if (data->read_func) {
		ret = data->read_func(target, data->cur_reg, val,
				      data->msg_byte, data->read_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			return 0;
		}
	}

	/* Emulator handler */
	if (data->read_byte) {
		k_mutex_lock(&data->data_mtx, K_FOREVER);
		ret = data->read_byte(target, data->cur_reg, val,
				      data->msg_byte);
		k_mutex_unlock(&data->data_mtx);
		if (ret) {
			return -EIO;
		}
	}

	return 0;
}

/** Check description in emul_common_i2c.h */
int i2c_common_emul_transfer_workhorse(const struct emul *target,
				       struct i2c_common_emul_data *data,
				       const struct i2c_common_emul_cfg *cfg,
				       struct i2c_msg *msgs, int num_msgs,
				       int addr)
{
	bool read, stop;
	int ret, i;

	if (cfg->addr != addr) {
		LOG_ERR("Address mismatch, expected %02x, got %02x", cfg->addr,
			addr);
		return -EIO;
	}

	i2c_dump_msgs(target->dev, msgs, num_msgs, addr);

	for (; num_msgs > 0; num_msgs--, msgs++) {
		read = msgs->flags & I2C_MSG_READ;
		stop = msgs->flags & I2C_MSG_STOP;

		switch (data->msg_state) {
		case I2C_COMMON_EMUL_IN_WRITE:
			if (read) {
				data->msg_state = I2C_COMMON_EMUL_NONE_MSG;
				ret = i2c_common_emul_finish_write(target,
								   data);
				if (ret) {
					return ret;
				}
				ret = i2c_common_emul_start_read(target, data);
				if (ret) {
					return ret;
				}
			}
			break;
		case I2C_COMMON_EMUL_IN_READ:
			if (!read) {
				data->msg_state = I2C_COMMON_EMUL_NONE_MSG;
				ret = i2c_common_emul_finish_read(target, data);
				if (ret) {
					return ret;
				}
				/* Wait for write message with acctual data */
				if (msgs->len == 0) {
					continue;
				}
				/* Dispatch command/register address */
				data->cur_reg = msgs->buf[0];
				ret = i2c_common_emul_start_write(target, data);
				if (ret) {
					return ret;
				}
			}
			break;
		case I2C_COMMON_EMUL_NONE_MSG:
			if (read) {
				ret = i2c_common_emul_start_read(target, data);
				if (ret) {
					return ret;
				}
			} else {
				/* Wait for write message with acctual data */
				if (msgs->len == 0) {
					continue;
				}
				/* Dispatch command/register address */
				data->cur_reg = msgs->buf[0];
				ret = i2c_common_emul_start_write(target, data);
				if (ret) {
					return ret;
				}
			}
		}

		data->msg_state = read ? I2C_COMMON_EMUL_IN_READ :
					 I2C_COMMON_EMUL_IN_WRITE;

		if (stop) {
			data->msg_state = I2C_COMMON_EMUL_NONE_MSG;
		}

		if (!read) {
			/*
			 * All current emulators use first byte of write message
			 * as command/register address for following write bytes
			 * or read message. Skip first byte which was dispatched
			 * already.
			 */
			if (data->msg_byte == 0) {
				data->msg_byte = 1;
				i = 1;
			} else {
				i = 0;
			}
			/* Dispatch write command */
			for (; i < msgs->len; i++, data->msg_byte++) {
				ret = i2c_common_emul_write_byte(target, data,
								 msgs->buf[i]);
				if (ret) {
					return ret;
				}
			}
			/* Finish write command */
			if (stop) {
				ret = i2c_common_emul_finish_write(target,
								   data);
				if (ret) {
					return ret;
				}
			}
		} else {
			/* Dispatch read command */
			for (i = 0; i < msgs->len; i++, data->msg_byte++) {
				ret = i2c_common_emul_read_byte(
					target, data, &(msgs->buf[i]));
				if (ret) {
					return ret;
				}
			}

			/* Finish read command */
			if (stop) {
				ret = i2c_common_emul_finish_read(target, data);
				if (ret) {
					return ret;
				}
			}
		}
	}

	return 0;
}

/** Check description in emul_common_i2c.h */
int i2c_common_emul_transfer(const struct emul *target, struct i2c_msg *msgs,
			     int num_msgs, int addr)
{
	const struct i2c_common_emul_cfg *cfg;
	struct i2c_common_emul_data *data;

	data = target->data;
	cfg = target->cfg;

	return i2c_common_emul_transfer_workhorse(target, data, cfg, msgs,
						  num_msgs, addr);
}

/** Check description in emul_common_i2c.h */
void i2c_common_emul_init(struct i2c_common_emul_data *data)
{
	data->msg_state = I2C_COMMON_EMUL_NONE_MSG;
	data->msg_byte = 0;
	data->cur_reg = 0;

	data->write_func = NULL;
	data->read_func = NULL;

	data->write_fail_reg = I2C_COMMON_EMUL_NO_FAIL_REG;
	data->read_fail_reg = I2C_COMMON_EMUL_NO_FAIL_REG;

	k_mutex_init(&data->data_mtx);
}

struct i2c_emul_api i2c_common_emul_api = {
	.transfer = i2c_common_emul_transfer,
};

static int i2c_common_emul_transfer_noop(const struct emul *target,
					 struct i2c_msg *msgs, int num_msgs,
					 int addr)
{
	return 0;
}

struct i2c_emul_api i2c_common_emul_noop = {
	.transfer = i2c_common_emul_transfer_noop,
};
