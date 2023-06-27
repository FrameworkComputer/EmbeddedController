/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_bmi260.h"
#include "driver/accelgyro_bmi_common.h"
#include "emul/emul_bmi.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zephyr_bmi_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_bmi260);

/** Mask reserved bits in each register of BMI260 */
static const uint8_t bmi_emul_260_rsvd_mask[] = {
	[BMI260_CHIP_ID] = 0x00,
	[0x01] = 0xff, /* Reserved */
	[BMI260_ERR_REG] = 0x20,
	[BMI260_STATUS] = 0x0b,
	[BMI260_AUX_X_L_G] = 0x00,
	[BMI260_AUX_X_H_G] = 0x00,
	[BMI260_AUX_Y_L_G] = 0x00,
	[BMI260_AUX_Y_H_G] = 0x00,
	[BMI260_AUX_Z_L_G] = 0x00,
	[BMI260_AUX_Z_H_G] = 0x00,
	[BMI260_AUX_R_L_G] = 0x00,
	[BMI260_AUX_R_H_G] = 0x00,
	[BMI260_ACC_X_L_G] = 0x00,
	[BMI260_ACC_X_H_G] = 0x00,
	[BMI260_ACC_Y_L_G] = 0x00,
	[BMI260_ACC_Y_H_G] = 0x00,
	[BMI260_ACC_Z_L_G] = 0x00,
	[BMI260_ACC_Z_H_G] = 0x00,
	[BMI260_GYR_X_L_G] = 0x00,
	[BMI260_GYR_X_H_G] = 0x00,
	[BMI260_GYR_Y_L_G] = 0x00,
	[BMI260_GYR_Y_H_G] = 0x00,
	[BMI260_GYR_Z_L_G] = 0x00,
	[BMI260_GYR_Z_H_G] = 0x00,
	[BMI260_SENSORTIME_0] = 0x00,
	[BMI260_SENSORTIME_1] = 0x00,
	[BMI260_SENSORTIME_2] = 0x00,
	[BMI260_EVENT] = 0xe2,
	[BMI260_INT_STATUS_0] = 0x00,
	[BMI260_INT_STATUS_1] = 0x18,
	[BMI260_SC_OUT_0] = 0x00,
	[BMI260_SC_OUT_1] = 0x00,
	[BMI260_ORIENT_ACT] = 0xe0,
	[BMI260_INTERNAL_STATUS] = 0x00,
	[BMI260_TEMPERATURE_0] = 0x00,
	[BMI260_TEMPERATURE_1] = 0x00,
	[BMI260_FIFO_LENGTH_0] = 0x00,
	[BMI260_FIFO_LENGTH_1] = 0xc0,
	[BMI160_FIFO_DATA] = 0x00,
	[0x27 ... 0x2e] = 0xff, /* Reserved */
	[BMI260_FEAT_PAGE] = 0xf8,
	[0x30 ... 0x3f] = 0x00, /* Features */
	[BMI260_ACC_CONF] = 0x00,
	[BMI260_ACC_RANGE] = 0xfc,
	[BMI260_GYR_CONF] = 0x00,
	[BMI260_GYR_RANGE] = 0xf0,
	[BMI260_AUX_CONF] = 0x00,
	[BMI260_FIFO_DOWNS] = 0x00,
	[BMI260_FIFO_WTM_0] = 0x00,
	[BMI260_FIFO_WTM_1] = 0xe0,
	[BMI260_FIFO_CONFIG_0] = 0xfc,
	[BMI260_FIFO_CONFIG_1] = 0x00,
	[BMI260_SATURATION] = 0xc0,
	[BMI260_AUX_DEV_ID] = 0x01,
	[BMI260_AUX_IF_CONF] = 0x30,
	[BMI260_AUX_RD_ADDR] = 0x00,
	[BMI260_AUX_WR_ADDR] = 0x00,
	[BMI260_AUX_WR_DATA] = 0x00,
	[0x50 ... 0x51] = 0xff, /* Reserved */
	[BMI260_ERR_REG_MSK] = 0x20,
	[BMI260_INT1_IO_CTRL] = 0xe1,
	[BMI260_INT2_IO_CTRL] = 0xe1,
	[BMI260_INT_LATCH] = 0xfe,
	[BMI260_INT1_MAP_FEAT] = 0x00,
	[BMI260_INT2_MAP_FEAT] = 0x00,
	[BMI260_INT_MAP_DATA] = 0x00,
	[BMI260_INIT_CTRL] = 0x00,
	[0x5a] = 0xff, /* Reserved */
	[BMI260_INIT_ADDR_0] = 0xf0,
	[BMI260_INIT_ADDR_1] = 0x00,
	[0x5d] = 0xff, /* Reserved */
	[BMI260_INIT_DATA] = 0x00,
	[BMI260_INTERNAL_ERROR] = 0xe9,
	[0x60 ... 0x67] = 0xff, /* Reserved */
	[BMI260_AUX_IF_TRIM] = 0xf8,
	[BMI260_GYR_CRT_CONF] = 0xf2,
	[BMI260_NVM_CONF] = 0xfd,
	[BMI260_IF_CONF] = 0xcc,
	[BMI260_DRV] = 0x00,
	[BMI260_ACC_SELF_TEST] = 0xf2,
	[BMI260_GYR_SELF_TEST_AXES] = 0xf0,
	[0x6f] = 0xff, /* Reserved */
	[BMI260_NV_CONF] = 0xf0,
	[BMI260_OFFSET_ACC70] = 0x00,
	[BMI260_OFFSET_ACC70 + 1] = 0x00,
	[BMI260_OFFSET_ACC70 + 2] = 0x00,
	[BMI260_OFFSET_GYR70] = 0x00,
	[BMI260_OFFSET_GYR70 + 1] = 0x00,
	[BMI260_OFFSET_GYR70 + 2] = 0x00,
	[BMI160_OFFSET_EN_GYR98] = 0x00,
	[0x78 ... 0x7b] = 0xff, /* Reserved */
	[BMI260_PWR_CONF] = 0xf8,
	[BMI260_PWR_CTRL] = 0xf0,
	[BMI260_CMD_REG] = 0x00,
};

/**
 * @brief Reset registers to default values and restore registers backed by NVM
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 */
static void bmi260_emul_reset(uint8_t *regs, const struct emul *emul)
{
	bool tag_time;
	bool header;

	regs[BMI260_CHIP_ID] = 0x27;
	regs[BMI260_ERR_REG] = 0x00;
	regs[BMI260_STATUS] = 0x10;
	regs[BMI260_AUX_X_L_G] = 0x00;
	regs[BMI260_AUX_X_H_G] = 0x00;
	regs[BMI260_AUX_Y_L_G] = 0x00;
	regs[BMI260_AUX_Y_H_G] = 0x00;
	regs[BMI260_AUX_Z_L_G] = 0x00;
	regs[BMI260_AUX_Z_H_G] = 0x00;
	regs[BMI260_AUX_R_L_G] = 0x00;
	regs[BMI260_AUX_R_H_G] = 0x00;
	regs[BMI260_ACC_X_L_G] = 0x00;
	regs[BMI260_ACC_X_H_G] = 0x00;
	regs[BMI260_ACC_Y_L_G] = 0x00;
	regs[BMI260_ACC_Y_H_G] = 0x00;
	regs[BMI260_ACC_Z_L_G] = 0x00;
	regs[BMI260_ACC_Z_H_G] = 0x00;
	regs[BMI260_GYR_X_L_G] = 0x00;
	regs[BMI260_GYR_X_H_G] = 0x00;
	regs[BMI260_GYR_Y_L_G] = 0x00;
	regs[BMI260_GYR_Y_H_G] = 0x00;
	regs[BMI260_GYR_Z_L_G] = 0x00;
	regs[BMI260_GYR_Z_H_G] = 0x00;
	regs[BMI260_SENSORTIME_0] = 0x00;
	regs[BMI260_SENSORTIME_1] = 0x00;
	regs[BMI260_SENSORTIME_2] = 0x00;
	regs[BMI260_EVENT] = 0x01;
	regs[BMI260_INT_STATUS_0] = 0x00;
	regs[BMI260_INT_STATUS_1] = 0x00;
	regs[BMI260_SC_OUT_0] = 0x00;
	regs[BMI260_SC_OUT_1] = 0x00;
	regs[BMI260_ORIENT_ACT] = 0x00;
	regs[BMI260_INTERNAL_STATUS] = 0x00;
	regs[BMI260_TEMPERATURE_0] = 0x00;
	regs[BMI260_TEMPERATURE_1] = 0x80;
	regs[BMI260_FIFO_LENGTH_0] = 0x00;
	regs[BMI260_FIFO_LENGTH_1] = 0x00;
	regs[BMI160_FIFO_DATA] = 0x00;
	regs[BMI260_FEAT_PAGE] = 0x00;
	regs[BMI260_ACC_CONF] = 0xa8;
	regs[BMI260_ACC_RANGE] = 0x02;
	regs[BMI260_GYR_CONF] = 0xa9;
	regs[BMI260_GYR_RANGE] = 0x00;
	regs[BMI260_AUX_CONF] = 0x46;
	regs[BMI260_FIFO_DOWNS] = 0x88;
	regs[BMI260_FIFO_WTM_0] = 0x00;
	regs[BMI260_FIFO_WTM_1] = 0x02;
	regs[BMI260_FIFO_CONFIG_0] = 0x02;
	regs[BMI260_FIFO_CONFIG_1] = 0x10;
	regs[BMI260_SATURATION] = 0x00;
	regs[BMI260_AUX_DEV_ID] = 0x20;
	regs[BMI260_AUX_IF_CONF] = 0x83;
	regs[BMI260_AUX_RD_ADDR] = 0x42;
	regs[BMI260_AUX_WR_ADDR] = 0x4c;
	regs[BMI260_AUX_WR_DATA] = 0x02;
	regs[BMI260_ERR_REG_MSK] = 0x00;
	regs[BMI260_INT1_IO_CTRL] = 0x00;
	regs[BMI260_INT2_IO_CTRL] = 0x00;
	regs[BMI260_INT_LATCH] = 0x00;
	regs[BMI260_INT1_MAP_FEAT] = 0x00;
	regs[BMI260_INT2_MAP_FEAT] = 0x00;
	regs[BMI260_INT_MAP_DATA] = 0x00;
	regs[BMI260_INIT_CTRL] = 0x00;
	regs[BMI260_INIT_ADDR_0] = 0x00;
	regs[BMI260_INIT_ADDR_1] = 0x00;
	regs[BMI260_INIT_DATA] = 0x00;
	regs[BMI260_INTERNAL_ERROR] = 0x00;
	regs[BMI260_AUX_IF_TRIM] = 0x01;
	regs[BMI260_GYR_CRT_CONF] = 0x00;
	regs[BMI260_NVM_CONF] = 0x00;
	regs[BMI260_IF_CONF] = 0x00;
	regs[BMI260_DRV] = 0xff;
	regs[BMI260_ACC_SELF_TEST] = 0x00;
	regs[BMI260_GYR_SELF_TEST_AXES] = 0x00;
	regs[BMI260_PWR_CONF] = 0x03;
	regs[BMI260_PWR_CTRL] = 0x00;
	regs[BMI260_CMD_REG] = 0x00;

	/* Call generic reset */
	tag_time = regs[BMI260_FIFO_CONFIG_0] & BMI260_FIFO_TIME_EN;
	header = regs[BMI260_FIFO_CONFIG_1] & BMI260_FIFO_HEADER_EN;
	bmi_emul_reset_common(emul, tag_time, header);
}

/**
 * @brief Convert range in format of ACC_RANGE register to number of bits
 *        that should be shifted right to obtain 16 bit reported accelerometer
 *        value from internal 32 bit value
 *
 * @param range Value of ACC_RANGE register
 *
 * @return shift Number of LSB that should be ignored from internal
 *               accelerometer value
 */
static int bmi260_emul_acc_range_to_shift(uint8_t range)
{
	switch (range & 0xf) {
	case BMI260_GSEL_2G:
		return 0;
	case BMI260_GSEL_4G:
		return 1;
	case BMI260_GSEL_8G:
		return 2;
	case BMI260_GSEL_16G:
		return 3;
	default:
		return 0;
	}
}

/**
 * @brief Convert range in format of GYR_RANGE register to number of bits
 *        that should be shifted right to obtain 16 bit reported gyroscope
 *        value from internal 32 bit value
 *
 * @param range Value of GYR_RANGE register
 *
 * @return shift Number of LSB that should be ignored from internal
 *               gyroscope value
 */
static int bmi260_emul_gyr_range_to_shift(uint8_t range)
{
	switch (range & 0x7) {
	case BMI260_DPS_SEL_2000:
		return 4;
	case BMI260_DPS_SEL_1000:
		return 3;
	case BMI260_DPS_SEL_500:
		return 2;
	case BMI260_DPS_SEL_250:
		return 1;
	case BMI260_DPS_SEL_125:
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
static int bmi260_emul_start_cmd(uint8_t *regs, const struct emul *emul,
				 int cmd)
{
	int time;

	switch (cmd) {
	case BMI260_CMD_SOFT_RESET:
		time = 1;
		break;
	case BMI260_CMD_FIFO_FLUSH:
		time = 0;
		break;
	default:
		LOG_ERR("Unknown command 0x%x", cmd);
		return -EIO;
	}

	regs[BMI260_CMD_REG] = cmd;
	bmi_emul_set_cmd_end_time(emul, time);

	return 0;
}

/**
 * @brief Emulate end of ongoing command.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 */
static void bmi260_emul_end_cmd(uint8_t *regs, const struct emul *emul)
{
	bool tag_time;
	bool header;
	int cmd;

	cmd = regs[BMI260_CMD_REG];
	regs[BMI260_CMD_REG] = 0;
	tag_time = regs[BMI260_FIFO_CONFIG_0] & BMI260_FIFO_TIME_EN;
	header = regs[BMI260_FIFO_CONFIG_1] & BMI260_FIFO_HEADER_EN;

	switch (cmd) {
	case BMI160_CMD_SOFT_RESET:
		bmi260_emul_reset(regs, emul);
		break;
	case BMI160_CMD_FIFO_FLUSH:
		bmi_emul_flush_fifo(emul, tag_time, header);
		break;
	}
}

/**
 * @brief Get currently accessed register. It is first register plus number of
 *        handled bytes for all registers except BMI260_FIFO_DATA and
 *        BMI260_INIT_DATA for which address incrementation is disabled.
 *
 * @param emul Pointer to BMI emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int bmi260_emul_access_reg(const struct emul *emul, int reg, int byte,
				  bool read)
{
	/* Ignore first byte which sets starting register */
	if (!read) {
		byte -= 1;
	}

	/*
	 * If register is FIFO data, then read data from FIFO.
	 * Init data is also block, but it is not implemented in emulator.
	 * Else block read access subsequent registers.
	 */
	if (reg <= BMI260_FIFO_DATA && reg + byte >= BMI260_FIFO_DATA) {
		return BMI260_FIFO_DATA;
	} else if (reg <= BMI260_INIT_DATA && reg + byte >= BMI260_INIT_DATA) {
		return BMI260_INIT_DATA;
	}

	return reg + byte;
}

/**
 * @brief BMI260 specific write function. It handle block writes. Init data
 *        register is trap register, so after reaching it, register address
 *        is not increased on block writes. Check if read only register is not
 *        accessed. Before writing value, ongoing command is finished if
 *        possible. Write to CMD register is handled by BMI260 specific
 *        function. On changing of FIFO header/headerless mode or
 *        enabling/disabling sensor in headerless mode FIFO is flushed.
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
static int bmi260_emul_handle_write(uint8_t *regs, const struct emul *emul,
				    int reg, int byte, uint8_t val)
{
	uint8_t mask;
	bool tag_time;
	bool header;

	reg = bmi260_emul_access_reg(emul, reg, byte, false /* = read */);

	if (reg <= BMI260_FIFO_DATA || reg == BMI260_GYR_SELF_TEST_AXES ||
	    reg == BMI260_INTERNAL_ERROR || reg == BMI260_SATURATION) {
		return BMI_EMUL_ACCESS_E;
	}

	/* Stop on going command if required */
	if (regs[BMI260_CMD_REG] != 0 && bmi_emul_is_cmd_end(emul)) {
		bmi260_emul_end_cmd(regs, emul);
	}

	tag_time = regs[BMI260_FIFO_CONFIG_0] & BMI260_FIFO_TIME_EN;
	header = regs[BMI260_FIFO_CONFIG_1] & BMI260_FIFO_HEADER_EN;

	switch (reg) {
	case BMI260_CMD_REG:
		if (regs[BMI260_CMD_REG] != 0) {
			LOG_ERR("Issued command before previous end");
			return -EIO;
		}

		return bmi260_emul_start_cmd(regs, emul, val);
	case BMI260_FIFO_CONFIG_1:
		/*
		 * Clear FIFO on transition between headerless and
		 * header mode
		 */
		if (!!(val & BMI260_FIFO_HEADER_EN) != header) {
			bmi_emul_flush_fifo(emul, tag_time, header);
		}
		break;
	case BMI260_PWR_CTRL:
		/*
		 * Clear FIFO on enabling/disabling sensors in headerless
		 * mode
		 */
		mask = BMI260_AUX_EN & BMI260_GYR_EN & BMI260_ACC_EN;
		if ((val & mask) != (regs[BMI260_PWR_CTRL] & mask) && !header) {
			bmi_emul_flush_fifo(emul, tag_time, header);
		}
		break;
	}

	return 0;
}

/**
 * @brief BMI260 specific read function. It handle block reads. FIFO data
 *        register and init data register are trap registers, so
 *        after reaching it, register address is not increased on block reads.
 *        Before reading value, ongoing command is finished if possible.
 *        Read of sensor data traps current emulator state in registers.
 *        Read of FIFO length and FIFO data triggers default BMI functions.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 * @param reg Register address that is accessed
 * @param byte Byte which is accessed during block read
 * @param buf Pointer where read byte should be stored
 *
 * @return 0 on success
 * @return BMI_EMUL_ACCESS_E on WO register access
 * @return -EIO on other error
 */
static int bmi260_emul_handle_read(uint8_t *regs, const struct emul *emul,
				   int reg, int byte, char *buf)
{
	uint16_t fifo_len;
	bool acc_off_en;
	bool gyr_off_en;
	bool tag_time;
	bool header;
	int gyr_shift;
	int acc_shift;
	int fifo_byte;

	/* Get number of bytes readed from FIFO */
	fifo_byte = byte - (reg - BMI260_FIFO_DATA);

	reg = bmi260_emul_access_reg(emul, reg, byte, true /* = read */);

	if (reg == BMI260_CMD_REG) {
		*buf = 0;

		return BMI_EMUL_ACCESS_E;
	}

	/* Stop on going command if required */
	if (regs[BMI260_CMD_REG] != 0 && bmi_emul_is_cmd_end(emul)) {
		bmi260_emul_end_cmd(regs, emul);
	}

	tag_time = regs[BMI260_FIFO_CONFIG_0] & BMI260_FIFO_TIME_EN;
	header = regs[BMI260_FIFO_CONFIG_1] & BMI260_FIFO_HEADER_EN;
	acc_off_en = regs[BMI260_NV_CONF] & BMI260_ACC_OFFSET_EN;
	gyr_off_en = regs[BMI260_OFFSET_EN_GYR98] & BMI260_OFFSET_GYRO_EN;
	gyr_shift = bmi260_emul_gyr_range_to_shift(regs[BMI260_GYR_RANGE]);
	acc_shift = bmi260_emul_acc_range_to_shift(regs[BMI260_ACC_RANGE]);

	switch (reg) {
	case BMI260_GYR_X_L_G:
	case BMI260_GYR_X_H_G:
	case BMI260_GYR_Y_L_G:
	case BMI260_GYR_Y_H_G:
	case BMI260_GYR_Z_L_G:
	case BMI260_GYR_Z_H_G:
	case BMI260_ACC_X_L_G:
	case BMI260_ACC_X_H_G:
	case BMI260_ACC_Y_L_G:
	case BMI260_ACC_Y_H_G:
	case BMI260_ACC_Z_L_G:
	case BMI260_ACC_Z_H_G:
	case BMI260_SENSORTIME_0:
	case BMI260_SENSORTIME_1:
	case BMI260_SENSORTIME_2:
		/*
		 * Snapshot of current emulator state is created on data read
		 * and shouldn't be changed until next I2C operation
		 */
		if (byte == 0) {
			bmi_emul_state_to_reg(emul, acc_shift, gyr_shift,
					      BMI260_ACC_X_L_G,
					      BMI260_GYR_X_L_G,
					      BMI260_SENSORTIME_0, acc_off_en,
					      gyr_off_en);
		}
		break;
	case BMI260_FIFO_LENGTH_0:
	case BMI260_FIFO_LENGTH_1:
		if (byte == 0) {
			fifo_len = bmi_emul_fifo_len(emul, tag_time, header);
			regs[BMI260_FIFO_LENGTH_0] = fifo_len & 0xff;
			regs[BMI260_FIFO_LENGTH_1] = (fifo_len >> 8) & 0x7;
		}
		break;
	case BMI260_FIFO_DATA:
		regs[reg] = bmi_emul_get_fifo_data(emul, fifo_byte, tag_time,
						   header, acc_shift,
						   gyr_shift);
		break;
	}

	*buf = regs[reg];

	return 0;
}

/** Registers backed in NVM by BMI260 */
const int bmi260_nvm_reg[] = {
	BMI260_AUX_IF_TRIM,    BMI260_NV_CONF,		BMI260_DRV,
	BMI260_OFFSET_ACC70,   BMI260_OFFSET_ACC70 + 1, BMI260_OFFSET_ACC70 + 2,
	BMI260_OFFSET_GYR70,   BMI260_OFFSET_GYR70 + 1, BMI260_OFFSET_GYR70 + 2,
	BMI260_OFFSET_EN_GYR98
};

/** Confguration of BMI260 */
struct bmi_emul_type_data bmi260_emul = {
	.sensortime_follow_config_frame = true,
	.start_write = NULL,
	.handle_write = bmi260_emul_handle_write,
	.finish_write = NULL,
	.start_read = NULL,
	.handle_read = bmi260_emul_handle_read,
	.access_reg = bmi260_emul_access_reg,
	.reset = bmi260_emul_reset,
	.rsvd_mask = bmi_emul_260_rsvd_mask,
	.nvm_reg = bmi260_nvm_reg,
	.nvm_len = ARRAY_SIZE(bmi260_nvm_reg),
	.gyr_off_reg = BMI260_OFFSET_GYR70,
	.acc_off_reg = BMI260_OFFSET_ACC70,
	.gyr98_off_reg = BMI260_OFFSET_EN_GYR98,
	.frame_order = { BMI_EMUL_FRAME_MAG, BMI_EMUL_FRAME_GYR,
			 BMI_EMUL_FRAME_ACC, BMI_EMUL_FRAME_NONE },
	.reg_bytes = 1,
};

/** Check description in emul_bmi.h */
const struct bmi_emul_type_data *get_bmi260_emul_type_data(void)
{
	return &bmi260_emul;
}
