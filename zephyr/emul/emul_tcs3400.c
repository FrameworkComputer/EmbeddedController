/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT zephyr_tcs3400

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(emul_tcs);

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#include "emul/emul_tcs3400.h"

#include "driver/als_tcs3400.h"

/**
 * Describe if there is no ongoing I2C message or if there is message handled
 * at the moment (last message doesn't ended with stop or write is not followed
 * by read).
 */
enum tcs_emul_msg_state {
	TCS_EMUL_NONE_MSG,
	TCS_EMUL_IN_WRITE,
	TCS_EMUL_IN_READ
};

/** Run-time data used by the emulator */
struct tcs_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** TCS3400 device being emulated */
	const struct device *i2c;
	/** Configuration information */
	const struct tcs_emul_cfg *cfg;

	/** Current state of emulated TCS3400 registers */
	uint8_t reg[TCS_EMUL_REG_COUNT];
	/** Return IR value instead of clear */
	bool ir_select;
	/** Internal values of light sensor registers */
	int red;
	int green;
	int blue;
	int clear;
	int ir;

	/** ID registers value */
	uint8_t revision;
	uint8_t id;

	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;
	/** Return error when trying to access MSB before LSB */
	bool error_on_msb_first;
	/**
	 * Flag set when LSB register is accessed and cleared when MSB is
	 * accessed. Allows to track order of accessing data registers
	 */
	bool lsb_r_read;
	bool lsb_g_read;
	bool lsb_b_read;
	bool lsb_c_ir_read;

	/** Current state of I2C bus (if emulator is handling message) */
	enum tcs_emul_msg_state msg_state;
	/** Number of already handled bytes in ongoing message */
	int msg_byte;
	/** Register selected in last write command */
	uint8_t cur_reg;
	/** Value of data byte in ongoing write message */
	uint8_t write_byte;

	/** Custom write function called on I2C write opperation */
	tcs_emul_write_func write_func;
	/** Data passed to custom write function */
	void *write_func_data;
	/** Custom read function called on I2C read opperation */
	tcs_emul_read_func read_func;
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
struct tcs_emul_cfg {
	/** Label of the I2C bus this emulator connects to */
	const char *i2c_label;
	/** Pointer to run-time data */
	struct tcs_emul_data *data;
	/** Address of TCS3400 on i2c bus */
	uint16_t addr;
};

/** Check description in emul_tcs3400.h */
int tcs_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	return k_mutex_lock(&data->data_mtx, timeout);
}

/** Check description in emul_tcs3400.h */
int tcs_emul_unlock_data(struct i2c_emul *emul)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	return k_mutex_unlock(&data->data_mtx);
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_write_func(struct i2c_emul *emul,
			     tcs_emul_write_func func, void *data)
{
	struct tcs_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	emul_data->write_func = func;
	emul_data->write_func_data = data;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_read_func(struct i2c_emul *emul,
			    tcs_emul_read_func func, void *data)
{
	struct tcs_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	emul_data->read_func = func;
	emul_data->read_func_data = data;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct tcs_emul_data *data;

	if (reg < TCS_EMUL_FIRST_REG || reg > TCS_EMUL_LAST_REG) {
		return;
	}

	reg -= TCS_EMUL_FIRST_REG;
	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	data->reg[reg] = val;
}

/** Check description in emul_tcs3400.h */
uint8_t tcs_emul_get_reg(struct i2c_emul *emul, int reg)
{
	struct tcs_emul_data *data;

	if (reg < TCS_EMUL_FIRST_REG || reg > TCS_EMUL_LAST_REG) {
		return 0;
	}

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	reg -= TCS_EMUL_FIRST_REG;

	return data->reg[reg];
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_read_fail_reg(struct i2c_emul *emul, int reg)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	data->read_fail_reg = reg;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_write_fail_reg(struct i2c_emul *emul, int reg)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	data->write_fail_reg = reg;
}

/** Check description in emul_tcs3400.h */
int tcs_emul_get_val(struct i2c_emul *emul, enum tcs_emul_axis axis)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	switch (axis) {
	case TCS_EMUL_R:
		return data->red;
	case TCS_EMUL_G:
		return data->green;
	case TCS_EMUL_B:
		return data->blue;
	case TCS_EMUL_C:
		return data->clear;
	case TCS_EMUL_IR:
		return data->ir;
	}

	return 0;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_val(struct i2c_emul *emul, enum tcs_emul_axis axis, int val)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	switch (axis) {
	case TCS_EMUL_R:
		data->red = val;
		break;
	case TCS_EMUL_G:
		data->green = val;
		break;
	case TCS_EMUL_B:
		data->blue = val;
		break;
	case TCS_EMUL_C:
		data->clear = val;
		break;
	case TCS_EMUL_IR:
		data->ir = val;
		break;
	}
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	data->error_on_ro_write = set;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	data->error_on_rsvd_write = set;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_err_on_msb_first(struct i2c_emul *emul, bool set)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
	data->error_on_msb_first = set;
}

/** Mask reserved bits in registers of TCS3400 */
static const uint8_t tcs_emul_rsvd_mask[] = {
	[TCS_I2C_ENABLE  - TCS_EMUL_FIRST_REG]	= 0xa4,
	[TCS_I2C_ATIME   - TCS_EMUL_FIRST_REG]	= 0x00,
	[0x2]					= 0xff, /* Reserved */
	[TCS_I2C_WTIME   - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_AILTL   - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_AILTH   - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_AIHTL   - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_AIHTH   - TCS_EMUL_FIRST_REG]	= 0x00,
	[0x8 ... 0xb]				= 0xff, /* Reserved */
	[TCS_I2C_PERS    - TCS_EMUL_FIRST_REG]	= 0xf0,
	[TCS_I2C_CONFIG  - TCS_EMUL_FIRST_REG]	= 0x81,
	[0xe]					= 0xff, /* Reserved */
	[TCS_I2C_CONTROL - TCS_EMUL_FIRST_REG]	= 0xfc,
	[TCS_I2C_AUX     - TCS_EMUL_FIRST_REG]	= 0xdf,
	[TCS_I2C_REVID   - TCS_EMUL_FIRST_REG]	= 0xf0,
	[TCS_I2C_ID      - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_STATUS  - TCS_EMUL_FIRST_REG]	= 0x6e,
	[TCS_I2C_CDATAL  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_CDATAH  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_RDATAL  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_RDATAH  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_GDATAL  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_GDATAH  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_BDATAL  - TCS_EMUL_FIRST_REG]	= 0x00,
	[TCS_I2C_BDATAH  - TCS_EMUL_FIRST_REG]	= 0x00,
};

/**
 * @brief Reset registers to default values
 *
 * @param emul Pointer to TCS3400 emulator
 */
static void tcs_emul_reset(struct i2c_emul *emul)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	data->reg[TCS_I2C_ENABLE  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_ATIME   - TCS_EMUL_FIRST_REG] = 0xff;
	data->reg[TCS_I2C_WTIME   - TCS_EMUL_FIRST_REG] = 0xff;
	data->reg[TCS_I2C_AILTL   - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AILTH   - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AIHTL   - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AIHTH   - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_PERS    - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_CONFIG  - TCS_EMUL_FIRST_REG] = 0x40;
	data->reg[TCS_I2C_CONTROL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AUX     - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_REVID   - TCS_EMUL_FIRST_REG] = data->revision;
	data->reg[TCS_I2C_ID      - TCS_EMUL_FIRST_REG] = data->id;
	data->reg[TCS_I2C_STATUS  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_CDATAL  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_CDATAH  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_RDATAL  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_RDATAH  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_GDATAL  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_GDATAH  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_BDATAL  - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_BDATAH  - TCS_EMUL_FIRST_REG] = 0x00;

	data->ir_select = false;
}

/**
 * @brief Convert gain in format of CONTROL register to multiplyer
 *
 * @param control Value of CONTROL register
 *
 * @return gain by which messured value should be multiplied
 */
static int tcs_emul_get_gain(uint8_t control)
{
	switch (control & TCS_I2C_CONTROL_MASK) {
	case 0:
		return 1;
	case 1:
		return 4;
	case 2:
		return 16;
	case 3:
		return 64;
	default:
		return -1;
	}
}

/**
 * @brief Convert number of cycles in format of ATIME register
 *
 * @param atime Value of ATIME register
 *
 * @return cycles count that should be used to obtain light sensor values
 */
static int tcs_emul_get_cycles(uint8_t atime)
{
	return TCS_EMUL_MAX_CYCLES - (int)atime;
}

/**
 * @brief Clear all interrupt registers
 *
 * @param emul Pointer to TCS3400 emulator
 */
static void tcs_emul_clear_int(struct i2c_emul *emul)
{
	struct tcs_emul_data *data;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	data->reg[TCS_I2C_STATUS  - TCS_EMUL_FIRST_REG] = 0x00;
}

/**
 * @brief Handle I2C write message. It is checked if accessed register isn't RO
 *        and reserved bits are set to 0. Write set value of reg field of TCS
 *        emulator data ignoring reserved bits and write only bits. Some
 *        commands are handled specialy. Before any handling, custom function
 *        is called if provided.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register which is written
 * @param val Value being written to @p reg
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcs_emul_handle_write(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct tcs_emul_data *data;
	int ret;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	if (data->write_func) {
		ret = data->write_func(emul, reg, val, data->write_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			return 0;
		}
	}

	if (data->write_fail_reg == reg ||
	    data->write_fail_reg == TCS_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	/* Register is in data->reg */
	if (reg >= TCS_EMUL_FIRST_REG && reg <= TCS_EMUL_LAST_REG) {
		if (reg >= TCS_I2C_REVID && reg <= TCS_I2C_BDATAH) {
			if (data->error_on_ro_write) {
				LOG_ERR("Writing to reg 0x%x which is RO", reg);
				return -EIO;
			}

			return 0;
		}

		if (reg == TCS_I2C_CONFIG && data->error_on_rsvd_write &&
		    !(BIT(6) & val)) {
			LOG_ERR("CONFIG reg bit 6 is write as 6 (writing 0x%x)",
				val);
			return -EIO;
		}

		reg -= TCS_EMUL_FIRST_REG;
		if (data->error_on_rsvd_write &&
		    tcs_emul_rsvd_mask[reg] & val) {
			LOG_ERR("Writing 0x%x to reg 0x%x with rsvd mask 0x%x",
				val, reg + TCS_EMUL_FIRST_REG,
				tcs_emul_rsvd_mask[reg]);
			return -EIO;
		}

		/* Ignore all reserved bits */
		val &= ~tcs_emul_rsvd_mask[reg];
		val |= data->reg[reg] & tcs_emul_rsvd_mask[reg];

		data->reg[reg] = val;

		return 0;
	}

	switch (reg) {
	case TCS_I2C_IR:
		if (data->error_on_rsvd_write && 0x7f & val) {
			LOG_ERR("Writing 0x%x to reg 0x%x with rsvd mask 0x7f",
				val, reg);
			return -EIO;
		}
		data->ir_select = !!(val & BIT(7));
		break;
	case TCS_I2C_IFORCE:
		/* Interrupt generate is not supported */
		break;
	case TCS_I2C_CICLEAR:
	case TCS_I2C_AICLEAR:
		tcs_emul_clear_int(emul);
		break;
	default:
		/* Assume that other registers are RO */
		if (data->error_on_ro_write) {
			LOG_ERR("Writing to reg 0x%x which is RO (unknown)",
				reg);
			return -EIO;
		}
	}

	return 0;
}

/**
 * @brief Get set light sensor value for given register using internal
 *        state @p val. In case of accessing MSB check if LSB was accessed first
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg LSB or MSB register address. LSB has to be aligned to 2
 * @param lsb_read Pointer to variable which represent if last access to this
 *                 accelerometer value was through LSB register
 * @param lsb True if now accessing LSB, Flase if now accessing MSB
 * @param val Internal value of accessed light sensor
 *
 * @return 0 on success
 * @return -EIO when accessing MSB before LSB
 */
static int tcs_emul_get_reg_val(struct i2c_emul *emul, int reg,
				bool *lsb_read, bool lsb, unsigned int val)
{
	struct tcs_emul_data *data;
	uint64_t reg_val;
	int msb_reg;
	int lsb_reg;
	int cycles;
	int gain;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	if (lsb) {
		*lsb_read = 1;
	} else {
		/*
		 * If error on first accessing MSB is set and LSB wasn't
		 * accessed before, then return error.
		 */
		if (data->error_on_msb_first && !(*lsb_read)) {
			return -EIO;
		}
		*lsb_read = 0;
		/* LSB read should set correct value */
		return 0;
	}

	lsb_reg = (reg - TCS_EMUL_FIRST_REG) & ~(0x1);
	msb_reg = (reg - TCS_EMUL_FIRST_REG) | 0x1;

	gain = tcs_emul_get_gain(data->reg[TCS_I2C_CONTROL -
					   TCS_EMUL_FIRST_REG]);
	cycles = tcs_emul_get_cycles(data->reg[TCS_I2C_ATIME -
					       TCS_EMUL_FIRST_REG]);
	/*
	 * Internal value is with 256 cycles and x64 gain, so divide it to get
	 * registers value
	 */
	reg_val = (uint64_t)val * cycles * gain / TCS_EMUL_MAX_CYCLES /
		  TCS_EMUL_MAX_GAIN;

	if (reg_val > UINT16_MAX) {
		reg_val = UINT16_MAX;
	}

	data->reg[lsb_reg] = reg_val & 0xff;
	data->reg[msb_reg] = (reg_val >> 8) & 0xff;

	return 0;
}

/**
 * @brief Handle I2C read message. Response is obtained from reg field of TCS
 *        emul data. When accessing light sensor value, register data is first
 *        computed using internal emulator state. Before default handler, custom
 *        user read function is called if provided.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register address to read
 * @param buf Pointer where resultat should be stored
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcs_emul_handle_read(struct i2c_emul *emul, int reg, char *buf)
{
	struct tcs_emul_data *data;
	unsigned int c_ir;
	int ret;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);

	if (data->read_func) {
		ret = data->read_func(emul, reg, data->read_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			/* Immediately return value set by custom function */
			*buf = data->reg[reg - TCS_EMUL_FIRST_REG];

			return 0;
		}
	}

	if (data->read_fail_reg == reg ||
	    data->read_fail_reg == TCS_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	if ((reg < TCS_EMUL_FIRST_REG || reg > TCS_EMUL_LAST_REG) &&
	    reg != TCS_I2C_IR) {
		LOG_ERR("Accessing register 0x%x which cannot be read", reg);
		return -EIO;
	}

	switch (reg) {
	case TCS_I2C_CDATAL:
		/* Shouldn't fail for LSB */
		c_ir = data->ir_select ? data->ir : data->clear;
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_c_ir_read,
					   true, c_ir);
		break;
	case TCS_I2C_CDATAH:
		c_ir = data->ir_select ? data->ir : data->clear;
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_c_ir_read,
					   false, c_ir);
		if (ret) {
			LOG_ERR("MSB C read before LSB C");
			return -EIO;
		}
		break;
	case TCS_I2C_RDATAL:
		/* Shouldn't fail for LSB */
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_r_read,
					   true, data->red);
		break;
	case TCS_I2C_RDATAH:
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_r_read,
					   false, data->red);
		if (ret) {
			LOG_ERR("MSB R read before LSB R");
			return -EIO;
		}
		break;
	case TCS_I2C_GDATAL:
		/* Shouldn't fail for LSB */
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_g_read,
					   true, data->green);
		break;
	case TCS_I2C_GDATAH:
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_g_read,
					   false, data->green);
		if (ret) {
			LOG_ERR("MSB G read before LSB G");
			return -EIO;
		}
		break;
	case TCS_I2C_BDATAL:
		/* Shouldn't fail for LSB */
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_b_read,
					   true, data->blue);
		break;
	case TCS_I2C_BDATAH:
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_b_read,
					   false, data->blue);
		if (ret) {
			LOG_ERR("MSB B read before LSB B");
			return -EIO;
		}
		break;
	case TCS_I2C_IR:
		*buf = data->ir_select ? BIT(7) : 0;

		return 0;
	}

	*buf = data->reg[reg - TCS_EMUL_FIRST_REG];

	return 0;
}

/**
 * @biref Emulate an I2C transfer to a TCS3400 light sensor
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
static int tcs_emul_transfer(struct i2c_emul *emul, struct i2c_msg *msgs,
			     int num_msgs, int addr)
{
	const struct tcs_emul_cfg *cfg;
	struct tcs_emul_data *data;
	int ret, i, reg;
	bool read;

	data = CONTAINER_OF(emul, struct tcs_emul_data, emul);
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
		case TCS_EMUL_NONE_MSG:
			data->msg_byte = 0;
			break;
		case TCS_EMUL_IN_WRITE:
			if (read) {
				/* Finish write command */
				if (data->msg_byte == 2) {
					k_mutex_lock(&data->data_mtx,
						     K_FOREVER);
					ret = tcs_emul_handle_write(emul,
							data->cur_reg,
							data->write_byte);
					k_mutex_unlock(&data->data_mtx);
					if (ret) {
						return -EIO;
					}
				}
				data->msg_byte = 0;
			}
			break;
		case TCS_EMUL_IN_READ:
			if (!read) {
				data->msg_byte = 0;
			}
			break;
		}
		data->msg_state = read ? TCS_EMUL_IN_READ : TCS_EMUL_IN_WRITE;

		if (msgs->flags & I2C_MSG_STOP) {
			data->msg_state = TCS_EMUL_NONE_MSG;
		}

		if (!read) {
			/* Dispatch write command */
			for (i = 0; i < msgs->len; i++) {
				switch (data->msg_byte) {
				case 0:
					data->cur_reg = msgs->buf[i];
					break;
				case 1:
					data->write_byte = msgs->buf[i];
					break;
				default:
					data->msg_state = TCS_EMUL_NONE_MSG;
					LOG_ERR("Too long write command");
					return -EIO;
				}
				data->msg_byte++;
			}

			/* Execute write command */
			if (msgs->flags & I2C_MSG_STOP && data->msg_byte == 2) {
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = tcs_emul_handle_write(emul, data->cur_reg,
							    data->write_byte);
				k_mutex_unlock(&data->data_mtx);
				if (ret) {
					return -EIO;
				}
			}
		} else {
			/* Dispatch read command */
			for (i = 0; i < msgs->len; i++) {
				reg = data->cur_reg + data->msg_byte;
				data->msg_byte++;

				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = tcs_emul_handle_read(emul, reg,
							   &(msgs->buf[i]));
				k_mutex_unlock(&data->data_mtx);
				if (ret) {
					return -EIO;
				}
			}
		}
	}

	return 0;
}

/* Device instantiation */

static struct i2c_emul_api tcs_emul_api = {
	.transfer = tcs_emul_transfer,
};

/**
 * @brief Set up a new TCS3400 emulator
 *
 * This should be called for each TCS3400 device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int tcs_emul_init(const struct emul *emul,
			 const struct device *parent)
{
	const struct tcs_emul_cfg *cfg = emul->cfg;
	struct tcs_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &tcs_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;
	k_mutex_init(&data->data_mtx);

	ret = i2c_emul_register(parent, emul->dev_label, &data->emul);

	tcs_emul_reset(&data->emul);

	return ret;
}

#define TCS3400_EMUL(n)							\
	static struct tcs_emul_data tcs_emul_data_##n = {		\
		.revision = DT_INST_PROP(n, revision),			\
		.id = DT_ENUM_TOKEN(DT_DRV_INST(n), device_id),		\
		.error_on_ro_write = DT_INST_PROP(n, error_on_ro_write),\
		.error_on_rsvd_write = DT_INST_PROP(n,			\
					error_on_reserved_bit_write),	\
		.error_on_msb_first = DT_INST_PROP(n,			\
					error_on_msb_first_access),	\
		.lsb_c_ir_read = 0,					\
		.lsb_r_read = 0,					\
		.lsb_g_read = 0,					\
		.lsb_b_read = 0,					\
		.msg_state = TCS_EMUL_NONE_MSG,				\
		.cur_reg = 0,						\
		.write_func = NULL,					\
		.read_func = NULL,					\
		.write_fail_reg = TCS_EMUL_NO_FAIL_REG,			\
		.read_fail_reg = TCS_EMUL_NO_FAIL_REG,			\
	};								\
									\
	static const struct tcs_emul_cfg tcs_emul_cfg_##n = {		\
		.i2c_label = DT_INST_BUS_LABEL(n),			\
		.data = &tcs_emul_data_##n,				\
		.addr = DT_INST_REG_ADDR(n),				\
	};								\
	EMUL_DEFINE(tcs_emul_init, DT_DRV_INST(n), &tcs_emul_cfg_##n)

DT_INST_FOREACH_STATUS_OKAY(TCS3400_EMUL)

#define TCS3400_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &tcs_emul_data_##n.emul;

/** Check description in emul_tcs3400.h */
struct i2c_emul *tcs_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(TCS3400_EMUL_CASE)

	default:
		return NULL;
	}
}
