/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/accelgyro_bmi3xx.h"
#include "driver/accelgyro_bmi_common.h"
#include "emul/emul_bmi.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zephyr_bmi3xx_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_bmi3xx);

#define TO_REG8(x) (2 * (x))

/** Mask reserved bits in each register of BMI3XX */
static const uint16_t bmi3xx_emul_rsvd_mask[] = {
	[BMI3_REG_CHIP_ID] = 0xff00,
	[BMI3_REG_ERR_REG] = 0xffe1,
	[BMI3_REG_STATUS] = 0x0,
	[BMI3_REG_ACC_DATA_X] = 0x0,
	[BMI3_REG_ACC_DATA_Y] = 0x0,
	[BMI3_REG_ACC_DATA_Z] = 0x0,
	[BMI3_REG_GYR_DATA_X] = 0x0,
	[BMI3_REG_GYR_DATA_Y] = 0x0,
	[BMI3_REG_GYR_DATA_Z] = 0x0,
	[0x09 ... 0x0b] = 0x0,
	[0x0c] = 0xffc0,
	[BMI3_REG_INT_STATUS_INT1] = 0x0,
	[0x0e ... 0x10] = 0x0,
	[0x11] = 0xc200,
	[0x12 ... 0x13] = 0x0,
	[BMI3_FEATURE_IO_STATUS] = 0xfffe,
	[BMI3_REG_FIFO_FILL_LVL] = 0xf800,
	[BMI3_REG_FIFO_DATA] = 0x0,
	[0x17 ... 0x1f] = 0xffff,
	[BMI3_REG_ACC_CONF] = 0x8000,
	[BMI3_REG_GYR_CONF] = 0x8000,
	[0x22 ... 0x27] = 0xffff,
	[0x28 ... 0x29] = 0xc200,
	[0x2a] = 0xfeee,
	[0x2b] = 0xffee,
	[0x2c ... 0x34] = 0xffff,
	[BMI3_REG_FIFO_WATERMARK] = 0xfc00,
	[BMI3_REG_FIFO_CONF] = 0xf0fe,
	[BMI3_REG_FIFO_CTRL] = 0xfffe,
	[BMI3_REG_IO_INT_CTRL] = 0xf8f8,
	[BMI3_REG_IO_INT_CONF] = 0xfffe,
	[0x3a ... 0x3b] = 0x0,
	[0x3c ... 0x3f] = 0xffff,
	[BMI3_REG_UGAIN_OFF_SEL] = 0xffff,
	[BMI3_REG_FEATURE_ENGINE_GLOB_CTRL] = 0xfffe,
	[0x41] = 0xf800,
	[0x42] = 0x0,
	[0x43] = 0xfffc,
	[0x44] = 0xffff,
	[0x45] = 0xffc4,
	[0x46] = 0xffff,
	[0x47] = 0xffc0,
	[0x48 ... 0x4f] = 0xffff,
	[0x50] = 0xfffe,
	[0x51] = 0xfff0,
	[0x52] = 0xfffc,
	[0x53] = 0xffe0,
	[0x54 ... 0x5f] = 0xffff,
	/* b/240404661#comment27 For [0x60, 0x62, 0x64]: The mask in bmi323
	 * datasheet is actually 0xe000, but the sample code from Bosch uses
	 * 0xc0000. We use 0xc000 to follow the sample code.
	 */
	[0x60] = 0xc000,
	[0x61] = 0xff00,
	[0x62] = 0xc000,
	[0x63] = 0xff00,
	[0x64] = 0xc000,
	[0x65] = 0xff00,
	[0x66] = 0xfc00,
	[0x67] = 0xff80,
	[0x68] = 0xfc00,
	[0x69] = 0xff80,
	[0x6a] = 0xfc00,
	[0x6b] = 0xff80,
	[0x6c ... 0x6f] = 0xffff,
	[0x70] = 0x0,
	[0x71 ... 0x72] = 0xff00,
	[0x73 ... 0x7d] = 0xff,
	[BMI3_REG_CMD] = 0x0,
	[0x7f] = 0xff,
};

void bmi3xx_emul_reset(uint8_t *regs, const struct emul *emul)
{
	bool tag_time;
	uint16_t *regs16 = (uint16_t *)regs;

	regs16[BMI3_REG_CHIP_ID] = 0x0043;
	regs16[BMI3_REG_ERR_REG] = 0x0;
	regs16[BMI3_REG_STATUS] = 0x1;
	regs16[BMI3_REG_ACC_DATA_X] = 0x8000;
	regs16[BMI3_REG_ACC_DATA_Y] = 0x8000;
	regs16[BMI3_REG_ACC_DATA_Z] = 0x8000;
	regs16[BMI3_REG_GYR_DATA_X] = 0x8000;
	regs16[BMI3_REG_GYR_DATA_Y] = 0x8000;
	regs16[BMI3_REG_GYR_DATA_Z] = 0x8000;
	regs16[0x09] = 0x8000;
	for (int i = 0x0a; i <= 0x13; i++) {
		regs16[i] = 0x0;
	}
	regs16[BMI3_FEATURE_IO_STATUS] = 0x18;
	for (int i = 0x15; i <= 0x1f; i++) {
		regs16[i] = 0x0;
	}
	regs16[BMI3_REG_ACC_CONF] = 0x28;
	regs16[BMI3_REG_GYR_CONF] = 0x48;
	for (int i = 0x22; i <= 0x27; i++) {
		regs16[i] = 0x0;
	}
	regs16[0x28] = 0x3206;
	regs16[0x29] = 0x1206;
	for (int i = 0x2a; i <= 0x50; i++) {
		regs16[i] = 0x0;
	}
	regs16[0x51] = 0xa;
	for (int i = 0x52; i <= 0x7f; i++) {
		regs16[i] = 0x0;
	}

	/* Call generic reset */
	tag_time = regs16[BMI3_REG_FIFO_CONF] & (BMI3_FIFO_TIME_EN << 8);
	bmi_emul_reset_common(emul, tag_time, false);
}

static int bmi3xx_emul_acc_range_to_shift(uint16_t range)
{
	switch ((range >> 4) & 0x7) {
	case BMI3_ACC_RANGE_2G:
		return 0;
	case BMI3_ACC_RANGE_4G:
		return 1;
	case BMI3_ACC_RANGE_8G:
		return 2;
	case BMI3_ACC_RANGE_16G:
		return 3;
	default:
		return 0;
	}
}

static int bmi3xx_emul_gyr_range_to_shift(uint16_t range)
{
	switch ((range >> 4) & 0x7) {
	case BMI3_GYR_RANGE_2000DPS:
		return 4;
	case BMI3_GYR_RANGE_1000DPS:
		return 3;
	case BMI3_GYR_RANGE_500DPS:
		return 2;
	case BMI3_GYR_RANGE_250DPS:
		return 1;
	case BMI3_GYR_RANGE_125DPS:
		return 0;
	default:
		return 0;
	}
}

/**
 * @brief Execute first part of command. Emulate state of device which is
 *        during handling command (status bits etc). This function save time
 *        on which command should end.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 * @param cmd Command that is starting
 *
 * @return 0 on success
 * @return -EIO on failure
 */
static int bmi3xx_emul_start_cmd(uint8_t *regs, const struct emul *emul,
				 int cmd)
{
	int time;
	uint16_t *regs16 = (uint16_t *)regs;

	switch (cmd) {
	case BMI3_CMD_SELF_CALIB:
	case BMI3_CMD_SOFT_RESET:
		time = 1;
		break;
	default:
		LOG_ERR("Unknown command 0x%x", cmd);
		return -EIO;
	}

	regs16[BMI3_REG_CMD] = cmd;
	bmi_emul_set_cmd_end_time(emul, time);

	return 0;
}

/**
 * @brief Emulate end of ongoing command.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 */
static void bmi3xx_emul_end_cmd(uint8_t *regs, const struct emul *emul)
{
	bool tag_time;
	int cmd;
	uint16_t *regs16 = (uint16_t *)regs;

	cmd = regs16[BMI3_REG_CMD];
	regs16[BMI3_REG_CMD] = 0;
	tag_time = regs16[BMI3_REG_FIFO_CONF] & (BMI3_FIFO_TIME_EN << 8);

	switch (cmd) {
	case BMI3_CMD_SOFT_RESET:
		bmi3xx_emul_reset(regs, emul);
		break;
	case BMI3_CMD_SELF_CALIB:
		/* set calibration success */
		bmi_emul_set_reg16(emul, BMI3_FEATURE_IO_1,
				   BMI3_SC_ST_STATUS_MASK |
					   BMI3_SC_RESULT_MASK);
		break;
	default:
		break;
	}
}

/**
 * @brief Get currently accessed register in 8-bit register address. It is first
 *	  register plus number of handled bytes for all registers except
 *	  BMI3_REG_FIFO_DATA for which address incrementation is disabled.
 *
 * @param emul Pointer to BMI emulator
 * @param reg First byte of last write message in 16-bit register address
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int bmi3xx_emul_access_reg(const struct emul *emul, int reg, int byte,
				  bool read)
{
	if (!read) {
		/* Ignore first byte which sets starting register */
		byte -= 1;
	} else {
		/*
		 * Ignore first two bytes which sets starting register when
		 * reading
		 */
		byte = MAX(byte - 2, 0);
	}

	/*
	 * If register is FIFO data, then read data from FIFO.
	 * Init data is also block, but it is not implemented in emulator.
	 * Else block read access subsequent registers.
	 */
	if (REG16TO8(reg) <= REG16TO8(BMI3_REG_FIFO_DATA) &&
	    REG16TO8(reg) + byte >= REG16TO8(BMI3_REG_FIFO_DATA)) {
		return REG16TO8(BMI3_REG_FIFO_DATA);
	}

	return REG16TO8(reg) + byte;
}

/**
 * @brief BMI3XX specific write function. It handle block writes. Init data
 *        register is trap register, so after reaching it, register address
 *        is not increased on block writes. Check if read only register is not
 *        accessed.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 * @param reg Register address that is accessed
 * @param byte Number of handled bytes in this write command
 * @param val Value that is being written
 *
 * @return 0 on success
 * @return BMI_EMUL_ACCESS_E on RO register access
 * @return -EIO on error
 */
static int bmi3xx_emul_handle_write(uint8_t *regs, const struct emul *emul,
				    int reg, int byte, uint8_t val)
{
	uint16_t mask;
	bool tag_time;
	uint16_t *regs16 = (uint16_t *)regs;
	int shift_reg8, shift_reg16;

	shift_reg8 =
		bmi3xx_emul_access_reg(emul, reg, byte, false /* = read */);
	shift_reg16 = REG8TO16(shift_reg8);

	if (shift_reg16 < BMI3_REG_FIFO_DATA || shift_reg16 > BMI3_REG_CMD) {
		return BMI_EMUL_ACCESS_E;
	}

	tag_time = regs16[BMI3_REG_FIFO_CONF] & (BMI3_FIFO_TIME_EN << 8);

	switch (shift_reg16) {
	case BMI3_REG_FIFO_CONF:
		/*
		 * Clear FIFO on enabling/disabling sensors in headerless
		 * mode
		 */
		mask = BMI3_FIFO_ALL_EN << 8;
		if ((val & mask) != (regs16[BMI3_REG_FIFO_CONF] & mask)) {
			bmi_emul_flush_fifo(emul, tag_time, false);
		}
		break;
	}

	return 0;
}

static int bmi3xx_emul_start_read(uint8_t *regs, const struct emul *emul,
				  int reg)
{
	uint16_t *regs16 = (uint16_t *)regs;

	/* Stop on going command if required */
	if (regs16[BMI3_REG_CMD] && bmi_emul_is_cmd_end(emul)) {
		bmi3xx_emul_end_cmd(regs, emul);
	}

	return 0;
}

static int bmi3xx_emul_handle_read(uint8_t *regs, const struct emul *emul,
				   int reg, int byte, uint8_t *buf)
{
	uint16_t fifo_len;
	bool tag_time;
	int gyr_shift;
	int acc_shift;
	uint16_t *regs16 = (uint16_t *)regs;
	int shift_reg;
	int shift_reg16;

	/* Get number of bytes readed from FIFO */
	shift_reg = bmi3xx_emul_access_reg(emul, reg, byte, true /* = read */);
	shift_reg16 = REG8TO16(shift_reg);

	if (shift_reg16 == BMI3_REG_CMD) {
		*buf = 0;
		return BMI_EMUL_ACCESS_E;
	}

	tag_time = regs16[BMI3_REG_FIFO_CONF] & (BMI3_FIFO_TIME_EN << 8);
	gyr_shift = bmi3xx_emul_gyr_range_to_shift(regs16[BMI3_REG_GYR_CONF]);
	acc_shift = bmi3xx_emul_acc_range_to_shift(regs16[BMI3_REG_ACC_CONF]);

	switch (shift_reg16) {
	case BMI3_REG_ACC_DATA_X:
	case BMI3_REG_ACC_DATA_Y:
	case BMI3_REG_ACC_DATA_Z:
	case BMI3_REG_GYR_DATA_X:
	case BMI3_REG_GYR_DATA_Y:
	case BMI3_REG_GYR_DATA_Z:
	case BMI3_REG_TEMP_DATA:
	case BMI3_REG_SENSOR_TIME_0:
	case BMI3_REG_SENSOR_TIME_1:
		/*
		 * Snapshot of current emulator state is created on data read
		 * and shouldn't be changed until next I2C operation
		 */
		if (byte == 0) {
			bmi_emul_state_to_reg(emul, acc_shift, gyr_shift,
					      2 * BMI3_REG_ACC_DATA_X,
					      2 * BMI3_REG_GYR_DATA_X,
					      2 * BMI3_REG_SENSOR_TIME_0, true,
					      true);
		}
		break;
	case BMI3_REG_FIFO_FILL_LVL:
		if (byte == 0) {
			fifo_len = bmi_emul_fifo_len(emul, tag_time, false);
			/*
			 * The len is 2-bytes-len, and add an extra redundant
			 * byte 0x80
			 */
			fifo_len = fifo_len / 2 + 1;
			regs16[BMI3_REG_FIFO_FILL_LVL] = fifo_len & 0x7ff;
		}
		break;
	case BMI3_REG_FIFO_DATA:
		if (byte <= 1)
			break;
		regs[shift_reg] = bmi_emul_get_fifo_data(
			emul,
			byte - 2 /* byte 2 read from the start of the FIFO */,
			tag_time, false, acc_shift, gyr_shift);
		break;
	}

	*buf = regs[shift_reg];

	return 0;
}

static int bmi3xx_emul_finish_read(uint8_t *regs, const struct emul *emul,
				   int reg, int bytes)
{
	uint16_t *regs16 = (uint16_t *)regs;

	switch (reg) {
	/* Read clear registers */
	case BMI3_REG_STATUS:
		regs16[reg] = 0x0;
		break;
	default:
		break;
	}

	return 0;
}

static int bmi3xx_emul_start_write(uint8_t *regs, const struct emul *emul,
				   int reg)
{
	uint16_t *regs16 = (uint16_t *)regs;

	/* Stop on going command if required */
	if (regs16[BMI3_REG_CMD] && bmi_emul_is_cmd_end(emul)) {
		bmi3xx_emul_end_cmd(regs, emul);
	}

	return 0;
}

static int bmi3xx_emul_finish_write(uint8_t *regs, const struct emul *emul,
				    int reg, int bytes)
{
	uint16_t *regs16 = (uint16_t *)regs;

	switch (reg) {
	case BMI3_REG_CMD:
		return bmi3xx_emul_start_cmd(regs, emul, regs16[reg]);
	default:
		break;
	}

	return 0;
}

struct bmi_emul_type_data bmi3xx_emul = {
	.sensortime_follow_config_frame = false,
	.handle_write = bmi3xx_emul_handle_write,
	.handle_read = bmi3xx_emul_handle_read,
	.access_reg = bmi3xx_emul_access_reg,
	.reset = bmi3xx_emul_reset,
	.rsvd_mask = (const uint8_t *)bmi3xx_emul_rsvd_mask,
	.start_read = bmi3xx_emul_start_read,
	.finish_read = bmi3xx_emul_finish_read,
	.start_write = bmi3xx_emul_start_write,
	.finish_write = bmi3xx_emul_finish_write,
	.gyr_off_reg = BMI3_GYR_DP_OFF_X,
	.acc_off_reg = BMI3_ACC_DP_OFF_X,
	.nvm_reg = NULL,
	.nvm_len = 0,
	.gyr98_off_reg = 0,
	.frame_order = { BMI_EMUL_FRAME_ACC, BMI_EMUL_FRAME_GYR,
			 BMI_EMUL_FRAME_TEMP, BMI_EMUL_FRAME_TIME,
			 BMI_EMUL_FRAME_NONE },
	.reg_bytes = 2,
};

const struct bmi_emul_type_data *get_bmi3xx_emul_type_data(void)
{
	return &bmi3xx_emul;
}
