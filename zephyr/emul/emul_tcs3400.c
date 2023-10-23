/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/als_tcs3400.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/emul_tcs3400.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT zephyr_tcs3400_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_tcs);

/** Run-time data used by the emulator */
struct tcs_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Value of data byte in ongoing write message */
	uint8_t write_byte;

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
};

/** Check description in emul_tcs3400.h */
void tcs_emul_set_reg(const struct emul *emul, int reg, uint8_t val)
{
	struct tcs_emul_data *data;

	if (reg < TCS_EMUL_FIRST_REG || reg > TCS_EMUL_LAST_REG) {
		return;
	}

	reg -= TCS_EMUL_FIRST_REG;
	data = emul->data;
	data->reg[reg] = val;
}

/** Check description in emul_tcs3400.h */
uint8_t tcs_emul_get_reg(const struct emul *emul, int reg)
{
	struct tcs_emul_data *data;

	if (reg < TCS_EMUL_FIRST_REG || reg > TCS_EMUL_LAST_REG) {
		return 0;
	}

	data = emul->data;
	reg -= TCS_EMUL_FIRST_REG;

	return data->reg[reg];
}

/** Check description in emul_tcs3400.h */
int tcs_emul_get_val(const struct emul *emul, enum tcs_emul_axis axis)
{
	struct tcs_emul_data *data;

	data = emul->data;

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
void tcs_emul_set_val(const struct emul *emul, enum tcs_emul_axis axis, int val)
{
	struct tcs_emul_data *data;

	data = emul->data;

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
void tcs_emul_set_err_on_ro_write(const struct emul *emul, bool set)
{
	struct tcs_emul_data *data;

	data = emul->data;
	data->error_on_ro_write = set;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_err_on_rsvd_write(const struct emul *emul, bool set)
{
	struct tcs_emul_data *data;

	data = emul->data;
	data->error_on_rsvd_write = set;
}

/** Check description in emul_tcs3400.h */
void tcs_emul_set_err_on_msb_first(const struct emul *emul, bool set)
{
	struct tcs_emul_data *data;

	data = emul->data;
	data->error_on_msb_first = set;
}

/** Mask reserved bits in registers of TCS3400 */
static const uint8_t tcs_emul_rsvd_mask[] = {
	[TCS_I2C_ENABLE - TCS_EMUL_FIRST_REG] = 0xa4,
	[TCS_I2C_ATIME - TCS_EMUL_FIRST_REG] = 0x00,
	[0x2] = 0xff, /* Reserved */
	[TCS_I2C_WTIME - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_AILTL - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_AILTH - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_AIHTL - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_AIHTH - TCS_EMUL_FIRST_REG] = 0x00,
	[0x8 ... 0xb] = 0xff, /* Reserved */
	[TCS_I2C_PERS - TCS_EMUL_FIRST_REG] = 0xf0,
	[TCS_I2C_CONFIG - TCS_EMUL_FIRST_REG] = 0x81,
	[0xe] = 0xff, /* Reserved */
	[TCS_I2C_CONTROL - TCS_EMUL_FIRST_REG] = 0xfc,
	[TCS_I2C_AUX - TCS_EMUL_FIRST_REG] = 0xdf,
	[TCS_I2C_REVID - TCS_EMUL_FIRST_REG] = 0xf0,
	[TCS_I2C_ID - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_STATUS - TCS_EMUL_FIRST_REG] = 0x6e,
	[TCS_I2C_CDATAL - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_CDATAH - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_RDATAL - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_RDATAH - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_GDATAL - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_GDATAH - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_BDATAL - TCS_EMUL_FIRST_REG] = 0x00,
	[TCS_I2C_BDATAH - TCS_EMUL_FIRST_REG] = 0x00,
};

/**
 * @brief Reset registers to default values
 *
 * @param emul Pointer to TCS3400 emulator
 */
static void tcs_emul_reset(const struct emul *emul)
{
	struct tcs_emul_data *data;

	data = emul->data;

	data->reg[TCS_I2C_ENABLE - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_ATIME - TCS_EMUL_FIRST_REG] = 0xff;
	data->reg[TCS_I2C_WTIME - TCS_EMUL_FIRST_REG] = 0xff;
	data->reg[TCS_I2C_AILTL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AILTH - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AIHTL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AIHTH - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_PERS - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_CONFIG - TCS_EMUL_FIRST_REG] = 0x40;
	data->reg[TCS_I2C_CONTROL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_AUX - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_REVID - TCS_EMUL_FIRST_REG] = data->revision;
	data->reg[TCS_I2C_ID - TCS_EMUL_FIRST_REG] = data->id;
	data->reg[TCS_I2C_STATUS - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_CDATAL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_CDATAH - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_RDATAL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_RDATAH - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_GDATAL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_GDATAH - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_BDATAL - TCS_EMUL_FIRST_REG] = 0x00;
	data->reg[TCS_I2C_BDATAH - TCS_EMUL_FIRST_REG] = 0x00;

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
static void tcs_emul_clear_int(const struct emul *emul)
{
	struct tcs_emul_data *data;

	data = emul->data;

	data->reg[TCS_I2C_STATUS - TCS_EMUL_FIRST_REG] = 0x00;
}

/**
 * @brief Handle I2C write message. It is checked if accessed register isn't RO
 *        and reserved bits are set to 0. Write set value of reg field of TCS
 *        emulator data ignoring reserved bits and write only bits. Some
 *        commands are handled specialy.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register which is written
 * @param bytes Number of bytes received in this write message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcs_emul_handle_write(const struct emul *emul, int reg, int bytes)
{
	struct tcs_emul_data *data;
	uint8_t val;

	data = emul->data;

	/* This write only selected register for I2C read message */
	if (bytes < 2) {
		return 0;
	}

	val = data->write_byte;

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
static int tcs_emul_get_reg_val(const struct emul *emul, int reg,
				bool *lsb_read, bool lsb, unsigned int val)
{
	struct tcs_emul_data *data;
	uint64_t reg_val;
	int msb_reg;
	int lsb_reg;
	int cycles;
	int gain;

	data = emul->data;

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

	gain = tcs_emul_get_gain(
		data->reg[TCS_I2C_CONTROL - TCS_EMUL_FIRST_REG]);
	cycles = tcs_emul_get_cycles(
		data->reg[TCS_I2C_ATIME - TCS_EMUL_FIRST_REG]);
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
 *        computed using internal emulator state.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg First register address that is accessed in this read message
 * @param buf Pointer where result should be stored
 * @param bytes Number of bytes already handled in this read message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcs_emul_handle_read(const struct emul *emul, int reg, uint8_t *buf,
				int bytes)
{
	struct tcs_emul_data *data;
	unsigned int c_ir;
	int ret;

	data = emul->data;

	reg += bytes;

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
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_r_read, true,
					   data->red);
		break;
	case TCS_I2C_RDATAH:
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_r_read, false,
					   data->red);
		if (ret) {
			LOG_ERR("MSB R read before LSB R");
			return -EIO;
		}
		break;
	case TCS_I2C_GDATAL:
		/* Shouldn't fail for LSB */
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_g_read, true,
					   data->green);
		break;
	case TCS_I2C_GDATAH:
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_g_read, false,
					   data->green);
		if (ret) {
			LOG_ERR("MSB G read before LSB G");
			return -EIO;
		}
		break;
	case TCS_I2C_BDATAL:
		/* Shouldn't fail for LSB */
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_b_read, true,
					   data->blue);
		break;
	case TCS_I2C_BDATAH:
		ret = tcs_emul_get_reg_val(emul, reg, &data->lsb_b_read, false,
					   data->blue);
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
 * @brief Handle I2C write message. Check if message is not too long and saves
 *        data that will be stored in register
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register address that is accessed
 * @param val Data to write to the register
 * @param bytes Number of bytes already handled in this read message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcs_emul_write_byte(const struct emul *emul, int reg, uint8_t val,
			       int bytes)
{
	struct tcs_emul_data *data;

	data = emul->data;

	if (bytes > 1) {
		LOG_ERR("Too long write command");
		return -EIO;
	}

	data->write_byte = val;

	return 0;
}

/* Device instantiation */

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
static int tcs_emul_init(const struct emul *emul, const struct device *parent)
{
	struct tcs_emul_data *data = emul->data;

	data->common.i2c = parent;

	i2c_common_emul_init(&data->common);

	tcs_emul_reset(emul);

	return 0;
}

#define TCS3400_EMUL(n)                                              \
	static struct tcs_emul_data tcs_emul_data_##n = {		\
		.revision = DT_INST_PROP(n, revision),			\
		.id = DT_STRING_TOKEN(DT_DRV_INST(n), device_id),		\
		.error_on_ro_write = DT_INST_PROP(n, error_on_ro_write),\
		.error_on_rsvd_write = DT_INST_PROP(n,			\
					error_on_reserved_bit_write),	\
		.error_on_msb_first = DT_INST_PROP(n,			\
					error_on_msb_first_access),	\
		.lsb_c_ir_read = 0,					\
		.lsb_r_read = 0,					\
		.lsb_g_read = 0,					\
		.lsb_b_read = 0,					\
		.common = {						\
			.start_write = NULL,				\
			.write_byte = tcs_emul_write_byte,		\
			.finish_write = tcs_emul_handle_write,		\
			.start_read = NULL,				\
			.read_byte = tcs_emul_handle_read,		\
			.finish_read = NULL,				\
			.access_reg = NULL,				\
		},							\
	};         \
                                                                     \
	static const struct i2c_common_emul_cfg tcs_emul_cfg_##n = { \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),      \
		.data = &tcs_emul_data_##n.common,                   \
		.addr = DT_INST_REG_ADDR(n),                         \
	};                                                           \
	EMUL_DT_INST_DEFINE(n, tcs_emul_init, &tcs_emul_data_##n,    \
			    &tcs_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(TCS3400_EMUL)

#ifdef CONFIG_ZTEST
#define TCS3400_EMUL_RESET_RULE_BEFORE(n) \
	tcs_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)));

static void emul_tcs3400_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	DT_INST_FOREACH_STATUS_OKAY(TCS3400_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(emul_tcs3400_reset, emul_tcs3400_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_tcs3400_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
