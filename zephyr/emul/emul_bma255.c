/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT zephyr_bma255

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(emul_bma255);

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#include "emul/emul_bma255.h"

#include "driver/accel_bma2x2.h"

/**
 * Describe if there is no ongoing I2C message or if there is message handled
 * at the moment (last message doesn't ended with stop or write is not followed
 * by read).
 */
enum bma_emul_msg_state {
	BMA_EMUL_NONE_MSG,
	BMA_EMUL_IN_WRITE,
	BMA_EMUL_IN_READ
};

/** Run-time data used by the emulator */
struct bma_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** BMA255 device being emulated */
	const struct device *i2c;
	/** Configuration information */
	const struct bma_emul_cfg *cfg;

	/** Current state of all emulated BMA255 registers */
	uint8_t reg[0x40];
	/** Current state of NVM where offset and GP0/1 can be saved */
	uint8_t nvm_x;
	uint8_t nvm_y;
	uint8_t nvm_z;
	uint8_t nvm_gp0;
	uint8_t nvm_gp1;
	/** Internal offset values used in calculations */
	int16_t off_x;
	int16_t off_y;
	int16_t off_z;
	/** Internal values of accelerometr */
	int16_t acc_x;
	int16_t acc_y;
	int16_t acc_z;

	/**
	 * Return error when trying to start offset compensation when not ready
	 * flag is set.
	 */
	bool error_on_cal_trg_nrdy;
	/**
	 * Return error when trying to start offset compensation with range
	 * set to value different than 2G.
	 */
	bool error_on_cal_trg_bad_range;
	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;
	/** Return error when trying to access MSB before LSB */
	bool error_on_msb_first;
	/**
	 * Flag set when LSB register is accessed and cleared when MSB is
	 * accessed. Allows to track order of accessing acc registers
	 */
	bool lsb_x_read;
	bool lsb_y_read;
	bool lsb_z_read;

	/** Current state of I2C bus (if emulator is handling message) */
	enum bma_emul_msg_state msg_state;
	/** Number of already handled bytes in ongoing message */
	int msg_byte;
	/** Register selected in last write command */
	uint8_t cur_reg;
	/** Value of data byte in ongoing write message */
	uint8_t write_byte;

	/** Custom write function called on I2C write opperation */
	bma_emul_write_func write_func;
	/** Data passed to custom write function */
	void *write_func_data;
	/** Custom read function called on I2C read opperation */
	bma_emul_read_func read_func;
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
struct bma_emul_cfg {
	/** Label of the I2C bus this emulator connects to */
	const char *i2c_label;
	/** Pointer to run-time data */
	struct bma_emul_data *data;
	/** Address of BMA255 on i2c bus */
	uint16_t addr;
};

/** Check description in emul_bma255.h */
int bma_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	return k_mutex_lock(&data->data_mtx, timeout);
}

/** Check description in emul_bma255.h */
int bma_emul_unlock_data(struct i2c_emul *emul)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	return k_mutex_unlock(&data->data_mtx);
}

/** Check description in emul_bma255.h */
void bma_emul_set_write_func(struct i2c_emul *emul,
			     bma_emul_write_func func, void *data)
{
	struct bma_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	emul_data->write_func = func;
	emul_data->write_func_data = data;
}

/** Check description in emul_bma255.h */
void bma_emul_set_read_func(struct i2c_emul *emul,
			    bma_emul_read_func func, void *data)
{
	struct bma_emul_data *emul_data;

	emul_data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	emul_data->read_func = func;
	emul_data->read_func_data = data;
}

/** Check description in emul_bma255.h */
void bma_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct bma_emul_data *data;

	if (reg < 0 || reg > BMA2x2_FIFO_DATA_OUTPUT_ADDR) {
		return;
	}

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->reg[reg] = val;
}

/** Check description in emul_bma255.h */
uint8_t bma_emul_get_reg(struct i2c_emul *emul, int reg)
{
	struct bma_emul_data *data;

	if (reg < 0 || reg > BMA2x2_FIFO_DATA_OUTPUT_ADDR) {
		return 0;
	}

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	return data->reg[reg];
}

/** Check description in emul_bma255.h */
void bma_emul_set_read_fail_reg(struct i2c_emul *emul, int reg)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->read_fail_reg = reg;
}

/** Check description in emul_bma255.h */
void bma_emul_set_write_fail_reg(struct i2c_emul *emul, int reg)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->write_fail_reg = reg;
}

/**
 * @brief Convert @p val to two's complement representation. It makes sure that
 *        bit representation is correct even on platforms which represent
 *        signed inteager in different format. Unsigned bit representation
 *        allows to use well defined bitwise operations on returned value.
 *
 * @param val Inteager that is converted
 *
 * @return two's complement representation of @p val
 */
static uint16_t bma_emul_val_to_twos_comp(int16_t val)
{
	uint16_t twos_comp_val;

	/* Make sure that value is converted to twos compliment format */
	if (val < 0) {
		twos_comp_val = (uint16_t)(-val);
		twos_comp_val = ~twos_comp_val + 1;
	} else {
		twos_comp_val = (uint16_t)val;
	}

	return twos_comp_val;
}

/**
 * @brief Convert value from NVM format (8bit, 0x01 == 7.8mg) to internal
 *        offset format (16bit, 0x01 == 0.97mg).
 *
 * @param nvm Value in NVM format (8bit, 0x01 == 7.8mg). This is binary
 *            representation of two's complement signed number.
 *
 * @return offset Internal representation of @p nvm (16bit, 0x01 == 0.97mg)
 */
static int16_t bma_emul_nvm_to_off(uint8_t nvm)
{
	int16_t offset;
	int8_t sign;

	if (nvm & BIT(7)) {
		sign = -1;
		/* NVM value is in two's complement format */
		nvm = ~nvm + 1;
	} else {
		sign = 1;
	}

	offset = (int16_t)nvm;
	/* LSB in NVM is 7.8mg, while LSB in internal offset is 0.97mg */
	offset *= sign * 8;

	return offset;
}

/**
 * @brief Convert value from internal offset format (16bit, 0x01 == 0.97mg) to
 *        NVM format (8bit, 0x01 == 7.8mg). Function makes sure that NVM value
 *        is representation of two's complement signed number.
 *
 * @param val Value in internal offset format (16bit, 0x01 == 0.97mg).
 *
 * @return nvm NVM format representation of @p val (8bit, 0x01 == 7.8mg)
 */
static uint8_t bma_emul_off_to_nvm(int16_t off)
{
	uint16_t twos_comp_val;
	uint8_t nvm = 0;

	twos_comp_val = bma_emul_val_to_twos_comp(off);

	/*
	 * LSB in internal representation has value 0.97mg, while in NVM
	 * LSB is 7.8mg. Skip 0.97mg, 1.9mg and 3.9mg bits.
	 */
	nvm |= (twos_comp_val >> 3) & 0x7f;
	/* Set sign bit */
	nvm |= (twos_comp_val & BIT(15)) ? BIT(7) : 0x00;

	return nvm;
}

/** Check description in emul_bma255.h */
int16_t bma_emul_get_off(struct i2c_emul *emul, int axis)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	switch (axis) {
	case BMA_EMUL_AXIS_X:
		return data->off_x;
	case BMA_EMUL_AXIS_Y:
		return data->off_y;
	case BMA_EMUL_AXIS_Z:
		return data->off_z;
	}

	return 0;
}

/** Check description in emul_bma255.h */
void bma_emul_set_off(struct i2c_emul *emul, int axis, int16_t val)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	switch (axis) {
	case BMA_EMUL_AXIS_X:
		data->off_x = val;
		data->reg[BMA2x2_OFFSET_X_AXIS_ADDR] = bma_emul_off_to_nvm(
								data->off_x);
		break;
	case BMA_EMUL_AXIS_Y:
		data->off_y = val;
		data->reg[BMA2x2_OFFSET_Y_AXIS_ADDR] = bma_emul_off_to_nvm(
								data->off_y);
		break;
	case BMA_EMUL_AXIS_Z:
		data->off_z = val;
		data->reg[BMA2x2_OFFSET_Z_AXIS_ADDR] = bma_emul_off_to_nvm(
								data->off_z);
		break;
	}
}

/** Check description in emul_bma255.h */
int16_t bma_emul_get_acc(struct i2c_emul *emul, int axis)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	switch (axis) {
	case BMA_EMUL_AXIS_X:
		return data->acc_x;
	case BMA_EMUL_AXIS_Y:
		return data->acc_y;
	case BMA_EMUL_AXIS_Z:
		return data->acc_z;
	}

	return 0;
}

/** Check description in emul_bma255.h */
void bma_emul_set_acc(struct i2c_emul *emul, int axis, int16_t val)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	switch (axis) {
	case BMA_EMUL_AXIS_X:
		data->acc_x = val;
		break;
	case BMA_EMUL_AXIS_Y:
		data->acc_y = val;
		break;
	case BMA_EMUL_AXIS_Z:
		data->acc_z = val;
		break;
	}
}

/** Check description in emul_bma255.h */
void bma_emul_set_err_on_cal_nrdy(struct i2c_emul *emul, bool set)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->error_on_cal_trg_nrdy = set;
}

/** Check description in emul_bma255.h */
void bma_emul_set_err_on_cal_bad_range(struct i2c_emul *emul, bool set)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->error_on_cal_trg_bad_range = set;
}

/** Check description in emul_bma255.h */
void bma_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->error_on_ro_write = set;
}

/** Check description in emul_bma255.h */
void bma_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->error_on_rsvd_write = set;
}

/** Check description in emul_bma255.h */
void bma_emul_set_err_on_msb_first(struct i2c_emul *emul, bool set)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
	data->error_on_msb_first = set;
}

/** Mask reserved bits in each register of BMA255 */
static const uint8_t bma_emul_rsvd_mask[] = {
	[BMA2x2_CHIP_ID_ADDR]			= 0x00,
	[0x01]					= 0xff, /* Reserved */
	[BMA2x2_X_AXIS_LSB_ADDR]		= 0x0e,
	[BMA2x2_X_AXIS_MSB_ADDR]		= 0x00,
	[BMA2x2_Y_AXIS_LSB_ADDR]		= 0x0e,
	[BMA2x2_Y_AXIS_MSB_ADDR]		= 0x00,
	[BMA2x2_Z_AXIS_LSB_ADDR]		= 0x0e,
	[BMA2x2_Z_AXIS_MSB_ADDR]		= 0x00,
	[BMA2x2_TEMP_ADDR]			= 0x00,
	[BMA2x2_STAT1_ADDR]			= 0x00,
	[BMA2x2_STAT2_ADDR]			= 0x1f,
	[BMA2x2_STAT_TAP_SLOPE_ADDR]		= 0x00,
	[BMA2x2_STAT_ORIENT_HIGH_ADDR]		= 0x00,
	[0x0d]					= 0xff, /* Reserved */
	[BMA2x2_STAT_FIFO_ADDR]			= 0x00,
	[BMA2x2_RANGE_SELECT_ADDR]		= 0xf0,
	[BMA2x2_BW_SELECT_ADDR]			= 0xe0,
	[BMA2x2_MODE_CTRL_ADDR]			= 0x01,
	[BMA2x2_LOW_NOISE_CTRL_ADDR]		= 0x9f,
	[BMA2x2_DATA_CTRL_ADDR]			= 0x3f,
	[BMA2x2_RST_ADDR]			= 0x00,
	[0x15]					= 0xff, /* Reserved */
	[BMA2x2_INTR_ENABLE1_ADDR]		= 0x08,
	[BMA2x2_INTR_ENABLE2_ADDR]		= 0x80,
	[BMA2x2_INTR_SLOW_NO_MOTION_ADDR]	= 0xf0,
	[BMA2x2_INTR1_PAD_SELECT_ADDR]		= 0x00,
	[BMA2x2_INTR_DATA_SELECT_ADDR]		= 0x18,
	[BMA2x2_INTR2_PAD_SELECT_ADDR]		= 0x00,
	[0x1c]					= 0xff, /* Reserved */
	[0x1d]					= 0xff, /* Reserved */
	[BMA2x2_INTR_SOURCE_ADDR]		= 0xc0,
	[0x1f]					= 0xff, /* Reserved */
	[BMA2x2_INTR_SET_ADDR]			= 0xf0,
	[BMA2x2_INTR_CTRL_ADDR]			= 0x70,
	[BMA2x2_LOW_DURN_ADDR]			= 0x00,
	[BMA2x2_LOW_THRES_ADDR]			= 0x00,
	[BMA2x2_LOW_HIGH_HYST_ADDR]		= 0x38,
	[BMA2x2_HIGH_DURN_ADDR]			= 0x00,
	[BMA2x2_HIGH_THRES_ADDR]		= 0x00,
	[BMA2x2_SLOPE_DURN_ADDR]		= 0x00,
	[BMA2x2_SLOPE_THRES_ADDR]		= 0x00,
	[BMA2x2_SLOW_NO_MOTION_THRES_ADDR]	= 0x00,
	[BMA2x2_TAP_PARAM_ADDR]			= 0x38,
	[BMA2x2_TAP_THRES_ADDR]			= 0x20,
	[BMA2x2_ORIENT_PARAM_ADDR]		= 0x80,
	[BMA2x2_THETA_BLOCK_ADDR]		= 0x80,
	[BMA2x2_THETA_FLAT_ADDR]		= 0xc0,
	[BMA2x2_FLAT_HOLD_TIME_ADDR]		= 0xc8,
	[BMA2x2_FIFO_WML_TRIG]			= 0xc0,
	[0x31]					= 0xff, /* Reserved */
	[BMA2x2_SELFTEST_ADDR]			= 0xf8,
	[BMA2x2_EEPROM_CTRL_ADDR]		= 0x00,
	[BMA2x2_SERIAL_CTRL_ADDR]		= 0xf8,
	[0x35]					= 0xff, /* Reserved */
	[BMA2x2_OFFSET_CTRL_ADDR]		= 0x08,
	[BMA2x2_OFC_SETTING_ADDR]		= 0x80,
	[BMA2x2_OFFSET_X_AXIS_ADDR]		= 0x00,
	[BMA2x2_OFFSET_Y_AXIS_ADDR]		= 0x00,
	[BMA2x2_OFFSET_Z_AXIS_ADDR]		= 0x00,
	[BMA2x2_GP0_ADDR]			= 0x00,
	[BMA2x2_GP1_ADDR]			= 0x00,
	[0x3d]					= 0xff, /* Reserved */
	[BMA2x2_FIFO_MODE_ADDR]			= 0x3c,
	[BMA2x2_FIFO_DATA_OUTPUT_ADDR]		= 0x00,
};

/**
 * @brief Reset register values and internal representation of offset and two
 *        general purpose registers
 *
 * @param emul Pointer to BMA255 emulator
 */
static void bma_emul_restore_nvm(struct i2c_emul *emul)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	/* Restore registers values */
	data->reg[BMA2x2_OFFSET_X_AXIS_ADDR] = data->nvm_x;
	data->reg[BMA2x2_OFFSET_Y_AXIS_ADDR] = data->nvm_y;
	data->reg[BMA2x2_OFFSET_Z_AXIS_ADDR] = data->nvm_z;
	data->reg[BMA2x2_GP0_ADDR] = data->nvm_gp0;
	data->reg[BMA2x2_GP1_ADDR] = data->nvm_gp1;

	/* Restore internal offset values */
	data->off_x = bma_emul_nvm_to_off(data->nvm_x);
	data->off_y = bma_emul_nvm_to_off(data->nvm_y);
	data->off_z = bma_emul_nvm_to_off(data->nvm_z);
}

/**
 * @brief Reset registers to default values and restore registers backed by NVM
 *
 * @param emul Pointer to BMA255 emulator
 */
static void bma_emul_reset(struct i2c_emul *emul)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	data->reg[BMA2x2_CHIP_ID_ADDR]			= 0xfa;
	data->reg[0x01]					= 0x00; /* Reserved */
	data->reg[BMA2x2_X_AXIS_LSB_ADDR]		= 0x00;
	data->reg[BMA2x2_X_AXIS_MSB_ADDR]		= 0x00;
	data->reg[BMA2x2_Y_AXIS_LSB_ADDR]		= 0x00;
	data->reg[BMA2x2_Y_AXIS_MSB_ADDR]		= 0x00;
	data->reg[BMA2x2_Z_AXIS_LSB_ADDR]		= 0x00;
	data->reg[BMA2x2_Z_AXIS_MSB_ADDR]		= 0x00;
	data->reg[BMA2x2_TEMP_ADDR]			= 0x00;
	data->reg[BMA2x2_STAT1_ADDR]			= 0x00;
	data->reg[BMA2x2_STAT2_ADDR]			= 0x00;
	data->reg[BMA2x2_STAT_TAP_SLOPE_ADDR]		= 0x00;
	data->reg[BMA2x2_STAT_ORIENT_HIGH_ADDR]		= 0x00;
	data->reg[0x0d]					= 0xff; /* Reserved */
	data->reg[BMA2x2_STAT_FIFO_ADDR]		= 0x00;
	data->reg[BMA2x2_RANGE_SELECT_ADDR]		= 0x03;
	data->reg[BMA2x2_BW_SELECT_ADDR]		= 0x0f;
	data->reg[BMA2x2_MODE_CTRL_ADDR]		= 0x00;
	data->reg[BMA2x2_LOW_NOISE_CTRL_ADDR]		= 0x00;
	data->reg[BMA2x2_DATA_CTRL_ADDR]		= 0x00;
	data->reg[BMA2x2_RST_ADDR]			= 0x00;
	data->reg[0x15]					= 0xff; /* Reserved */
	data->reg[BMA2x2_INTR_ENABLE1_ADDR]		= 0x00;
	data->reg[BMA2x2_INTR_ENABLE2_ADDR]		= 0x00;
	data->reg[BMA2x2_INTR_SLOW_NO_MOTION_ADDR]	= 0x00;
	data->reg[BMA2x2_INTR1_PAD_SELECT_ADDR]		= 0x00;
	data->reg[BMA2x2_INTR_DATA_SELECT_ADDR]		= 0x00;
	data->reg[BMA2x2_INTR2_PAD_SELECT_ADDR]		= 0x00;
	data->reg[0x1c]					= 0xff; /* Reserved */
	data->reg[0x1d]					= 0xff; /* Reserved */
	data->reg[BMA2x2_INTR_SOURCE_ADDR]		= 0x00;
	data->reg[0x1f]					= 0xff; /* Reserved */
	data->reg[BMA2x2_INTR_SET_ADDR]			= 0x05;
	data->reg[BMA2x2_INTR_CTRL_ADDR]		= 0x00;
	data->reg[BMA2x2_LOW_DURN_ADDR]			= 0x09;
	data->reg[BMA2x2_LOW_THRES_ADDR]		= 0x30;
	data->reg[BMA2x2_LOW_HIGH_HYST_ADDR]		= 0x81;
	data->reg[BMA2x2_HIGH_DURN_ADDR]		= 0x0f;
	data->reg[BMA2x2_HIGH_THRES_ADDR]		= 0xc0;
	data->reg[BMA2x2_SLOPE_DURN_ADDR]		= 0x00;
	data->reg[BMA2x2_SLOPE_THRES_ADDR]		= 0x14;
	data->reg[BMA2x2_SLOW_NO_MOTION_THRES_ADDR]	= 0x14;
	data->reg[BMA2x2_TAP_PARAM_ADDR]		= 0x04;
	data->reg[BMA2x2_TAP_THRES_ADDR]		= 0x0a;
	data->reg[BMA2x2_ORIENT_PARAM_ADDR]		= 0x18;
	data->reg[BMA2x2_THETA_BLOCK_ADDR]		= 0x48;
	data->reg[BMA2x2_THETA_FLAT_ADDR]		= 0x08;
	data->reg[BMA2x2_FLAT_HOLD_TIME_ADDR]		= 0x11;
	data->reg[BMA2x2_FIFO_WML_TRIG]			= 0x00;
	data->reg[0x31]					= 0xff; /* Reserved */
	data->reg[BMA2x2_SELFTEST_ADDR]			= 0x00;
	data->reg[BMA2x2_EEPROM_CTRL_ADDR]		= 0xf0;
	data->reg[BMA2x2_SERIAL_CTRL_ADDR]		= 0x00;
	data->reg[0x35]					= 0x00; /* Reserved */
	data->reg[BMA2x2_OFFSET_CTRL_ADDR]		= 0x10;
	data->reg[BMA2x2_OFC_SETTING_ADDR]		= 0x00;
	data->reg[0x3d]					= 0xff; /* Reserved */
	data->reg[BMA2x2_FIFO_MODE_ADDR]		= 0x00;
	data->reg[BMA2x2_FIFO_DATA_OUTPUT_ADDR]		= 0x00;

	/* Restore registers backed in NVM */
	bma_emul_restore_nvm(emul);
}

/**
 * @brief Convert range in format of RANGE_SELECT register to number of bits
 *        that should be shifted right to obtain 12 bit reported accelerometer
 *        value from internal 16 bit value
 *
 * @param range Value of RANGE_SELECT register
 *
 * @return shift Number of LSB that should be ignored from internal
 *               accelerometer value
 */
static int bma_emul_range_to_shift(uint8_t range)
{
	switch (range & BMA2x2_RANGE_SELECT_MSK) {
	case BMA2x2_RANGE_2G:
		return 0;
	case BMA2x2_RANGE_4G:
		return 1;
	case BMA2x2_RANGE_8G:
		return 2;
	case BMA2x2_RANGE_16G:
		return 3;
	default:
		return -1;
	}
}

/**
 * @brief Handle write requests to NVM control register. Allows to load/store
 *        NVM only when ready in current NVM control register is set. Load and
 *        stores to NVM are backed in bma_emul_data structure
 *
 * @param emul Pointer to BMA255 emulator
 * @param val Value that is being written to NVM contorl register
 *
 * @return 0 on success
 */
static int bma_emul_handle_nvm_write(struct i2c_emul *emul, uint8_t val)
{
	struct bma_emul_data *data;
	uint8_t writes_rem;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	/* NVM not ready, ignore write/load requests */
	if (!(data->reg[BMA2x2_EEPROM_CTRL_ADDR] & BMA2x2_EEPROM_RDY)) {
		return 0;
	}

	/* Restore data from NVM */
	if (val & BMA2x2_EEPROM_LOAD) {
		bma_emul_restore_nvm(emul);
	}

	writes_rem = (data->reg[BMA2x2_EEPROM_CTRL_ADDR] &
		      BMA2x2_EEPROM_REMAIN_MSK) >> BMA2x2_EEPROM_REMAIN_OFF;
	/* Trigger write is set, write is unlocked and writes remaining */
	if (val & BMA2x2_EEPROM_PROG &&
	    data->reg[BMA2x2_EEPROM_CTRL_ADDR] & BMA2x2_EEPROM_PROG_EN &&
	    writes_rem > 0) {
		data->nvm_x = data->reg[BMA2x2_OFFSET_X_AXIS_ADDR];
		data->nvm_y = data->reg[BMA2x2_OFFSET_Y_AXIS_ADDR];
		data->nvm_z = data->reg[BMA2x2_OFFSET_Z_AXIS_ADDR];
		data->nvm_gp0 = data->reg[BMA2x2_GP0_ADDR];
		data->nvm_gp1 = data->reg[BMA2x2_GP1_ADDR];
		/* Decrement number of remaining writes and save it in reg */
		writes_rem--;
		data->reg[BMA2x2_EEPROM_CTRL_ADDR] &=
					~BMA2x2_EEPROM_REMAIN_MSK;
		data->reg[BMA2x2_EEPROM_CTRL_ADDR] |=
					writes_rem << BMA2x2_EEPROM_REMAIN_OFF;
	}

	return 0;
}

/**
 * @brief Clear all interrupt registers
 *
 * @param emul Pointer to BMA255 emulator
 */
static void bma_emul_clear_int(struct i2c_emul *emul)
{
	struct bma_emul_data *data;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	data->reg[BMA2x2_STAT1_ADDR]		= 0x00;
	data->reg[BMA2x2_STAT2_ADDR]		= 0x00;
	data->reg[BMA2x2_STAT_TAP_SLOPE_ADDR]	= 0x00;
	data->reg[BMA2x2_STAT_ORIENT_HIGH_ADDR]	= 0x00;
}

/**
 * @brief Get target value from offset compensation setting register for given
 *        @p axis
 *
 * @param emul Pointer to BMA255 emulator
 * @param axis Axis to access: 0 - X, 1 - Y, 2 - Z
 *
 * @return target Value to which offset compensation should be calculated
 */
static int16_t bma_emul_get_target(struct i2c_emul *emul, int axis)
{
	struct bma_emul_data *data;
	uint8_t target;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	target = data->reg[BMA2x2_OFC_SETTING_ADDR] >>
		 BMA2x2_OFC_TARGET_AXIS(axis);
	switch (target) {
	case BMA2x2_OFC_TARGET_0G:
		return 0;
	case BMA2x2_OFC_TARGET_PLUS_1G:
		return BMA_EMUL_1G;
	case BMA2x2_OFC_TARGET_MINUS_1G:
		return -((int)BMA_EMUL_1G);
	}

	return 0;
}

/**
 * @brief Handle writes to offset compensation control register. It allows to
 *        reset offset registers. It check if offset compenstation is ready
 *        and if range is set to 2G (required for fast compensation according
 *        to BMA255 documentation). If fast compensation is successfully
 *        triggered, internal offset value is set to
 *        (target - internal accelerometer value).
 *
 * @param emul Pointer to BMA255 emulator
 * @param val Value being written to offset compensation control register
 *
 * @return 0 on success
 * @return -EIO when trying to start fast compensation in wrong emulator state
 */
static int bma_emul_handle_off_comp(struct i2c_emul *emul, uint8_t val)
{
	struct bma_emul_data *data;
	uint8_t trigger;
	int16_t target;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	if (val & BMA2x2_OFFSET_RESET) {
		data->off_x = 0;
		data->off_y = 0;
		data->off_z = 0;
		data->reg[BMA2x2_OFFSET_X_AXIS_ADDR] = 0;
		data->reg[BMA2x2_OFFSET_Y_AXIS_ADDR] = 0;
		data->reg[BMA2x2_OFFSET_Z_AXIS_ADDR] = 0;
	}


	trigger = (val & BMA2x2_OFFSET_TRIGGER_MASK) >>
		  BMA2x2_OFFSET_TRIGGER_OFF;

	if (!(data->reg[BMA2x2_OFFSET_CTRL_ADDR] & BMA2x2_OFFSET_CAL_READY)) {
		if (data->error_on_cal_trg_nrdy && trigger) {
			LOG_ERR("Trying to start offset comp when not ready");
			return -EIO;
		}

		return 0;
	}

	if (bma_emul_range_to_shift(data->reg[BMA2x2_RANGE_SELECT_ADDR]) != 0 &&
	    trigger && data->error_on_cal_trg_bad_range) {
		LOG_ERR("Trying to start offset comp with range other than 2G");
		return -EIO;
	}

	switch (trigger) {
	case 1:
		target = bma_emul_get_target(emul, BMA_EMUL_AXIS_X);
		bma_emul_set_off(emul, BMA_EMUL_AXIS_X, target - data->acc_x);
		break;
	case 2:
		target = bma_emul_get_target(emul, BMA_EMUL_AXIS_Y);
		bma_emul_set_off(emul, BMA_EMUL_AXIS_Y, target - data->acc_y);
		break;
	case 3:
		target = bma_emul_get_target(emul, BMA_EMUL_AXIS_Z);
		bma_emul_set_off(emul, BMA_EMUL_AXIS_Z, target - data->acc_z);
		break;
	}

	return 0;
}

/**
 * @brief Handle I2C write message. It is checked if accessed register isn't RO
 *        and reserved bits are set to 0. Write set value of reg field of bma
 *        emulator data ignoring reserved bits and write only bits. Some
 *        commands are handled specialy. Before any handling, custom function
 *        is called if provided.
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Register which is written
 * @param val Value being written to @p reg
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bma_emul_handle_write(struct i2c_emul *emul, int reg, uint8_t val)
{
	struct bma_emul_data *data;
	int ret;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	if (data->write_func) {
		ret = data->write_func(emul, reg, val, data->write_func_data);
		if (ret < 0) {
			return -EIO;
		} else if (ret == 0) {
			return 0;
		}
	}

	if (data->write_fail_reg == reg ||
	    data->write_fail_reg == BMA_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	if (reg <= BMA2x2_STAT_FIFO_ADDR ||
	    reg >= BMA2x2_FIFO_DATA_OUTPUT_ADDR) {
		if (data->error_on_ro_write) {
			LOG_ERR("Writing to reg 0x%x which is RO", reg);
			return -EIO;
		}

		return 0;
	}

	if (data->error_on_rsvd_write && bma_emul_rsvd_mask[reg] & val) {
		LOG_ERR("Writing 0x%x to reg 0x%x with rsvd bits mask 0x%x",
			val, reg, bma_emul_rsvd_mask[reg]);
		return -EIO;
	}


	switch (reg) {
	case BMA2x2_RST_ADDR:
		if (val == BMA2x2_CMD_SOFT_RESET) {
			bma_emul_reset(emul);
		}
		return 0;
	case BMA2x2_INTR_CTRL_ADDR:
		if (val & BMA2x2_INTR_CTRL_RST_INT) {
			bma_emul_clear_int(emul);
		}
		/* Don't set write only bit in register */
		val &= ~BMA2x2_INTR_CTRL_RST_INT;
		break;
	case BMA2x2_EEPROM_CTRL_ADDR:
		bma_emul_handle_nvm_write(emul, val);
		/* Only programing enable bit is RW */
		val &= BMA2x2_EEPROM_PROG_EN;
		val |= data->reg[reg] & ~BMA2x2_EEPROM_PROG_EN;
		break;
	case BMA2x2_OFFSET_CTRL_ADDR:
		ret = bma_emul_handle_off_comp(emul, val);
		if (ret) {
			return -EIO;
		}
		/* Only slow compensation bits are RW */
		val &= BMA2x2_OFFSET_CAL_SLOW_X | BMA2x2_OFFSET_CAL_SLOW_Y |
		       BMA2x2_OFFSET_CAL_SLOW_Z;
		val |= data->reg[reg] & ~(BMA2x2_OFFSET_CAL_SLOW_X |
					  BMA2x2_OFFSET_CAL_SLOW_Y |
					  BMA2x2_OFFSET_CAL_SLOW_Z);
		break;
	/* Change internal offset to value set in I2C message */
	case BMA2x2_OFFSET_X_AXIS_ADDR:
		data->off_x = bma_emul_nvm_to_off(val);
		break;
	case BMA2x2_OFFSET_Y_AXIS_ADDR:
		data->off_y = bma_emul_nvm_to_off(val);
		break;
	case BMA2x2_OFFSET_Z_AXIS_ADDR:
		data->off_z = bma_emul_nvm_to_off(val);
		break;
	case BMA2x2_RANGE_SELECT_ADDR:
		ret = bma_emul_range_to_shift(val);
		if (ret < 0) {
			LOG_ERR("Unknown range select value 0x%x", val);
			return -EIO;
		}
		break;
	}

	/* Ignore all reserved bits */
	val &= ~bma_emul_rsvd_mask[reg];
	val |= data->reg[reg] & bma_emul_rsvd_mask[reg];

	data->reg[reg] = val;

	return 0;
}

/**
 * @brief Get set accelerometer value for given register using internal axis
 *        state @p val. In case of accessing MSB with enabled shadowing,
 *        check if LSB was accessed first.
 *
 * @param emul Pointer to BMA255 emulator
 * @param lsb_reg LSB register address (BMA2x2_X_AXIS_LSB_ADDR,
 *                BMA2x2_Y_AXIS_LSB_ADDR, BMA2x2_Z_AXIS_LSB_ADDR)
 * @param lsb_read Pointer to variable which represent if last access to this
 *                 accelerometer value was through LSB register
 * @param lsb True if now accessing LSB, Flase if now accessing MSB
 * @param val Internal value of accessed accelerometer axis
 *
 * @return 0 on success
 * @return -EIO when accessing MSB before LSB with enabled shadowing
 */
static int bma_emul_get_acc_val(struct i2c_emul *emul, int lsb_reg,
				bool *lsb_read, bool lsb, int16_t val)
{
	struct bma_emul_data *data;
	uint16_t twos_comp_val;
	uint8_t new_data;
	int msb_reg;
	int shift;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	if (lsb) {
		*lsb_read = 1;
	} else if (!(data->reg[BMA2x2_DATA_CTRL_ADDR] &
		     BMA2x2_DATA_SHADOW_DIS)) {
		/*
		 * If shadowing is enabled, error on first accessing MSB and
		 * LSB wasn't accessed before, then return error.
		 */
		if (data->error_on_msb_first && !(*lsb_read)) {
			return -EIO;
		}
		*lsb_read = 0;
		/* If shadowing is enabled, LSB read should set correct value */
		return 0;
	}

	twos_comp_val = bma_emul_val_to_twos_comp(val);
	msb_reg = lsb_reg + 1;
	shift = bma_emul_range_to_shift(data->reg[BMA2x2_RANGE_SELECT_ADDR]);

	/* Save new data bit from register */
	new_data = data->reg[lsb_reg] & BMA2x2_AXIS_LSB_NEW_DATA;
	/* Shift 16 bit value to 12 bit set in range register */
	twos_comp_val >>= shift;
	/* Set [3:0] bits in first register */
	data->reg[lsb_reg] = ((twos_comp_val << 4) & 0xf0) | new_data;
	/* Set [11:4] bits in second register */
	data->reg[msb_reg] = (twos_comp_val >> 4) & 0xff;

	return 0;
}

/**
 * @brief Handle I2C read message. Response is obtained from reg field of bma
 *        emul data. When accessing accelerometer value, register data is first
 *        computed using internal emulator state. Before default handler, custom
 *        user read function is called if provided.
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Register address to read
 * @param buf Pointer where resultat should be stored
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int bma_emul_handle_read(struct i2c_emul *emul, int reg, char *buf)
{
	struct bma_emul_data *data;
	int ret;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);

	if (reg > BMA2x2_FIFO_DATA_OUTPUT_ADDR) {
		reg = BMA2x2_FIFO_DATA_OUTPUT_ADDR;
	}

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
	    data->read_fail_reg == BMA_EMUL_FAIL_ALL_REG) {
		return -EIO;
	}

	switch (reg) {
	case BMA2x2_X_AXIS_LSB_ADDR:
		/* Shouldn't fail for LSB */
		ret = bma_emul_get_acc_val(emul, reg, &data->lsb_x_read, true,
					   data->acc_x + data->off_x);
		break;
	case BMA2x2_X_AXIS_MSB_ADDR:
		ret = bma_emul_get_acc_val(emul, reg - 1, &data->lsb_x_read,
					   false, data->acc_x + data->off_x);
		if (ret) {
			LOG_ERR("MSB X readed before LSB X");
			return -EIO;
		}
		break;
	case BMA2x2_Y_AXIS_LSB_ADDR:
		/* Shouldn't fail for LSB */
		ret = bma_emul_get_acc_val(emul, reg, &data->lsb_y_read, true,
					   data->acc_y + data->off_y);
		break;
	case BMA2x2_Y_AXIS_MSB_ADDR:
		ret = bma_emul_get_acc_val(emul, reg - 1, &data->lsb_y_read,
					   false, data->acc_y + data->off_y);
		if (ret) {
			LOG_ERR("MSB Y readed before LSB Y");
			return -EIO;
		}
		break;
	case BMA2x2_Z_AXIS_LSB_ADDR:
		/* Shouldn't fail for LSB */
		ret = bma_emul_get_acc_val(emul, reg, &data->lsb_z_read, true,
					   data->acc_z + data->off_z);
		break;
	case BMA2x2_Z_AXIS_MSB_ADDR:
		ret = bma_emul_get_acc_val(emul, reg - 1, &data->lsb_z_read,
					   false, data->acc_z + data->off_z);
		if (ret) {
			LOG_ERR("MSB Z readed before LSB Z");
			return -EIO;
		}
		break;
	}

	*buf = data->reg[reg];

	return 0;
}

/**
 * @biref Emulate an I2C transfer to a BMA255 accelerometer
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
static int bma_emul_transfer(struct i2c_emul *emul, struct i2c_msg *msgs,
			     int num_msgs, int addr)
{
	const struct bma_emul_cfg *cfg;
	struct bma_emul_data *data;
	int ret, i, reg;
	bool read;

	data = CONTAINER_OF(emul, struct bma_emul_data, emul);
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
		case BMA_EMUL_NONE_MSG:
			data->msg_byte = 0;
			break;
		case BMA_EMUL_IN_WRITE:
			if (read) {
				/* Finish write command */
				if (data->msg_byte == 2) {
					k_mutex_lock(&data->data_mtx,
						     K_FOREVER);
					ret = bma_emul_handle_write(emul,
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
		case BMA_EMUL_IN_READ:
			if (!read) {
				data->msg_byte = 0;
			}
			break;
		}
		data->msg_state = read ? BMA_EMUL_IN_READ : BMA_EMUL_IN_WRITE;

		if (msgs->flags & I2C_MSG_STOP) {
			data->msg_state = BMA_EMUL_NONE_MSG;
		}

		if (!read) {
			/* Dispatch wrtie command */
			for (i = 0; i < msgs->len; i++) {
				switch (data->msg_byte) {
				case 0:
					data->cur_reg = msgs->buf[i];
					break;
				case 1:
					data->write_byte = msgs->buf[i];
					break;
				default:
					data->msg_state = BMA_EMUL_NONE_MSG;
					LOG_ERR("Too long write command");
					return -EIO;
				}
				data->msg_byte++;
			}

			/* Execute write command */
			if (msgs->flags & I2C_MSG_STOP && data->msg_byte == 2) {
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = bma_emul_handle_write(emul, data->cur_reg,
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
				if (reg > BMA2x2_FIFO_DATA_OUTPUT_ADDR) {
					reg = BMA2x2_FIFO_DATA_OUTPUT_ADDR;
				} else {
					data->msg_byte++;
				}
				k_mutex_lock(&data->data_mtx, K_FOREVER);
				ret = bma_emul_handle_read(emul, reg,
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

static struct i2c_emul_api bma_emul_api = {
	.transfer = bma_emul_transfer,
};

/**
 * @brief Set up a new BMA255 emulator
 *
 * This should be called for each BMA255 device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int bma_emul_init(const struct emul *emul,
			 const struct device *parent)
{
	const struct bma_emul_cfg *cfg = emul->cfg;
	struct bma_emul_data *data = cfg->data;
	int ret;

	data->emul.api = &bma_emul_api;
	data->emul.addr = cfg->addr;
	data->i2c = parent;
	data->cfg = cfg;
	k_mutex_init(&data->data_mtx);

	ret = i2c_emul_register(parent, emul->dev_label, &data->emul);

	bma_emul_reset(&data->emul);

	return ret;
}

#define BMA255_EMUL(n)							\
	static struct bma_emul_data bma_emul_data_##n = {		\
		.nvm_x = DT_INST_PROP(n, nvm_off_x),			\
		.nvm_y = DT_INST_PROP(n, nvm_off_y),			\
		.nvm_z = DT_INST_PROP(n, nvm_off_z),			\
		.nvm_gp0 = DT_INST_PROP(n, nvm_gp0),			\
		.nvm_gp1 = DT_INST_PROP(n, nvm_gp1),			\
		.acc_x = DT_INST_PROP(n, nvm_acc_x),			\
		.acc_y = DT_INST_PROP(n, nvm_acc_y),			\
		.acc_z = DT_INST_PROP(n, nvm_acc_z),			\
		.error_on_cal_trg_nrdy = DT_INST_PROP(n,		\
				error_on_compensation_not_ready),	\
		.error_on_ro_write = DT_INST_PROP(n, error_on_ro_write),\
		.error_on_rsvd_write = DT_INST_PROP(n,			\
					error_on_reserved_bit_write),	\
		.error_on_msb_first = DT_INST_PROP(n,			\
					error_on_msb_first_access),	\
		.lsb_x_read = 0,					\
		.lsb_y_read = 0,					\
		.lsb_z_read = 0,					\
		.msg_state = BMA_EMUL_NONE_MSG,				\
		.cur_reg = 0,						\
		.write_func = NULL,					\
		.read_func = NULL,					\
		.write_fail_reg = BMA_EMUL_NO_FAIL_REG,			\
		.read_fail_reg = BMA_EMUL_NO_FAIL_REG,			\
	};								\
									\
	static const struct bma_emul_cfg bma_emul_cfg_##n = {		\
		.i2c_label = DT_INST_BUS_LABEL(n),			\
		.data = &bma_emul_data_##n,				\
		.addr = DT_INST_REG_ADDR(n),				\
	};								\
	EMUL_DEFINE(bma_emul_init, DT_DRV_INST(n), &bma_emul_cfg_##n)

DT_INST_FOREACH_STATUS_OKAY(BMA255_EMUL)

#define BMA255_EMUL_CASE(n)					\
	case DT_INST_DEP_ORD(n): return &bma_emul_data_##n.emul;

/** Check description in emul_bma255.h */
struct i2c_emul *bma_emul_get(int ord)
{
	switch (ord) {
	DT_INST_FOREACH_STATUS_OKAY(BMA255_EMUL_CASE)

	default:
		return NULL;
	}
}
