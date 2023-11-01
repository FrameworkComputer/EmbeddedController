/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_bmi160.h"
#include "driver/accelgyro_bmi_common.h"
#include "emul/emul_bmi.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zephyr_bmi_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_bmi160);

/** Mask reserved bits in each register of BMI160 */
static const uint8_t bmi_emul_160_rsvd_mask[] = {
	[BMI160_CHIP_ID] = 0x00,
	[0x01] = 0xff, /* Reserved */
	[BMI160_ERR_REG] = 0x00,
	[BMI160_PMU_STATUS] = 0xc0,
	[BMI160_MAG_X_L_G] = 0x00,
	[BMI160_MAG_X_H_G] = 0x00,
	[BMI160_MAG_Y_L_G] = 0x00,
	[BMI160_MAG_Y_H_G] = 0x00,
	[BMI160_MAG_Z_L_G] = 0x00,
	[BMI160_MAG_Z_H_G] = 0x00,
	[BMI160_RHALL_L_G] = 0x00,
	[BMI160_RHALL_H_G] = 0x00,
	[BMI160_GYR_X_L_G] = 0x00,
	[BMI160_GYR_X_H_G] = 0x00,
	[BMI160_GYR_Y_L_G] = 0x00,
	[BMI160_GYR_Y_H_G] = 0x00,
	[BMI160_GYR_Z_L_G] = 0x00,
	[BMI160_GYR_Z_H_G] = 0x00,
	[BMI160_ACC_X_L_G] = 0x00,
	[BMI160_ACC_X_H_G] = 0x00,
	[BMI160_ACC_Y_L_G] = 0x00,
	[BMI160_ACC_Y_H_G] = 0x00,
	[BMI160_ACC_Z_L_G] = 0x00,
	[BMI160_ACC_Z_H_G] = 0x00,
	[BMI160_SENSORTIME_0] = 0x00,
	[BMI160_SENSORTIME_1] = 0x00,
	[BMI160_SENSORTIME_2] = 0x00,
	[BMI160_STATUS] = 0x01,
	[BMI160_INT_STATUS_0] = 0x00,
	[BMI160_INT_STATUS_1] = 0x03,
	[BMI160_INT_STATUS_2] = 0x00,
	[BMI160_INT_STATUS_3] = 0x00,
	[BMI160_TEMPERATURE_0] = 0x00,
	[BMI160_TEMPERATURE_1] = 0x00,
	[BMI160_FIFO_LENGTH_0] = 0x00,
	[BMI160_FIFO_LENGTH_1] = 0xf8,
	[BMI160_FIFO_DATA] = 0x00,
	[0x25 ... 0x3f] = 0xff, /* Reserved */
	[BMI160_ACC_CONF] = 0x00,
	[BMI160_ACC_RANGE] = 0xf0,
	[BMI160_GYR_CONF] = 0xc0,
	[BMI160_GYR_RANGE] = 0xf8,
	[BMI160_MAG_CONF] = 0xf0,
	[BMI160_FIFO_DOWNS] = 0x00,
	[BMI160_FIFO_CONFIG_0] = 0x00,
	[BMI160_FIFO_CONFIG_1] = 0x01,
	[0x48 ... 0x4a] = 0xff, /* Reserved */
	[BMI160_MAG_IF_0] = 0x01,
	[BMI160_MAG_IF_1] = 0x40,
	[BMI160_MAG_IF_2] = 0x00,
	[BMI160_MAG_IF_3] = 0x00,
	[BMI160_MAG_IF_4] = 0x00,
	[BMI160_INT_EN_0] = 0x08,
	[BMI160_INT_EN_1] = 0x80,
	[BMI160_INT_EN_2] = 0xf0,
	[BMI160_INT_OUT_CTRL] = 0x00,
	[BMI160_INT_LATCH] = 0xc0,
	[BMI160_INT_MAP_0] = 0x00,
	[BMI160_INT_MAP_1] = 0x00,
	[BMI160_INT_MAP_2] = 0x00,
	[BMI160_INT_DATA_0] = 0x77,
	[BMI160_INT_DATA_1] = 0x7f,
	[BMI160_INT_LOW_HIGH_0] = 0x00,
	[BMI160_INT_LOW_HIGH_1] = 0x00,
	[BMI160_INT_LOW_HIGH_2] = 0x3c,
	[BMI160_INT_LOW_HIGH_3] = 0x00,
	[BMI160_INT_LOW_HIGH_4] = 0x00,
	[BMI160_INT_MOTION_0] = 0x00,
	[BMI160_INT_MOTION_1] = 0x00,
	[BMI160_INT_MOTION_2] = 0x00,
	[BMI160_INT_MOTION_3] = 0xc0,
	[BMI160_INT_TAP_0] = 0x38,
	[BMI160_INT_TAP_1] = 0xe0,
	[BMI160_INT_ORIENT_0] = 0x00,
	[BMI160_INT_ORIENT_1] = 0x00,
	[BMI160_INT_FLAT_0] = 0xc0,
	[BMI160_INT_FLAT_1] = 0xc8,
	[BMI160_FOC_CONF] = 0x80,
	[BMI160_CONF] = 0xfd,
	[BMI160_IF_CONF] = 0xce,
	[BMI160_PMU_TRIGGER] = 0x80,
	[BMI160_SELF_TEST] = 0xe0,
	[0x6e] = 0xff, /* Reserved */
	[0x6f] = 0xff, /* Reserved */
	[BMI160_NV_CONF] = 0xf0,
	[BMI160_OFFSET_ACC70] = 0x00,
	[BMI160_OFFSET_ACC70 + 1] = 0x00,
	[BMI160_OFFSET_ACC70 + 2] = 0x00,
	[BMI160_OFFSET_GYR70] = 0x00,
	[BMI160_OFFSET_GYR70 + 1] = 0x00,
	[BMI160_OFFSET_GYR70 + 2] = 0x00,
	[BMI160_OFFSET_EN_GYR98] = 0x00,
	[BMI160_STEP_CNT_0] = 0x00,
	[BMI160_STEP_CNT_1] = 0x00,
	[BMI160_STEP_CONF_0] = 0x00,
	[BMI160_STEP_CONF_1] = 0xf0,
	[0x7c] = 0xff, /* Reserved */
	[0x7d] = 0xff, /* Reserved */
	[BMI160_CMD_REG] = 0x00,
};

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
static int bmi160_emul_acc_range_to_shift(uint8_t range)
{
	switch (range & 0xf) {
	case BMI160_GSEL_2G:
		return 0;
	case BMI160_GSEL_4G:
		return 1;
	case BMI160_GSEL_8G:
		return 2;
	case BMI160_GSEL_16G:
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
static int bmi160_emul_gyr_range_to_shift(uint8_t range)
{
	switch (range & 0x7) {
	case BMI160_DPS_SEL_2000:
		return 4;
	case BMI160_DPS_SEL_1000:
		return 3;
	case BMI160_DPS_SEL_500:
		return 2;
	case BMI160_DPS_SEL_250:
		return 1;
	case BMI160_DPS_SEL_125:
		return 0;
	default:
		return 0;
	}
}

/**
 * @brief Reset registers to default values and restore registers backed by NVM
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 */
static void bmi160_emul_reset(uint8_t *regs, const struct emul *emul)
{
	bool tag_time;
	bool header;

	regs[BMI160_CHIP_ID] = 0xd1;
	regs[BMI160_ERR_REG] = 0x00;
	regs[BMI160_PMU_STATUS] = 0x00;
	regs[BMI160_MAG_X_L_G] = 0x00;
	regs[BMI160_MAG_X_H_G] = 0x00;
	regs[BMI160_MAG_Y_L_G] = 0x00;
	regs[BMI160_MAG_Y_H_G] = 0x00;
	regs[BMI160_MAG_Z_L_G] = 0x00;
	regs[BMI160_MAG_Z_H_G] = 0x00;
	regs[BMI160_RHALL_L_G] = 0x00;
	regs[BMI160_RHALL_H_G] = 0x00;
	regs[BMI160_GYR_X_L_G] = 0x00;
	regs[BMI160_GYR_X_H_G] = 0x00;
	regs[BMI160_GYR_Y_L_G] = 0x00;
	regs[BMI160_GYR_Y_H_G] = 0x00;
	regs[BMI160_GYR_Z_L_G] = 0x00;
	regs[BMI160_GYR_Z_H_G] = 0x00;
	regs[BMI160_ACC_X_L_G] = 0x00;
	regs[BMI160_ACC_X_H_G] = 0x00;
	regs[BMI160_ACC_Y_L_G] = 0x00;
	regs[BMI160_ACC_Y_H_G] = 0x00;
	regs[BMI160_ACC_Z_L_G] = 0x00;
	regs[BMI160_ACC_Z_H_G] = 0x00;
	regs[BMI160_SENSORTIME_0] = 0x00;
	regs[BMI160_SENSORTIME_1] = 0x00;
	regs[BMI160_SENSORTIME_2] = 0x00;
	regs[BMI160_STATUS] = 0x01;
	regs[BMI160_INT_STATUS_0] = 0x00;
	regs[BMI160_INT_STATUS_1] = 0x00;
	regs[BMI160_INT_STATUS_2] = 0x00;
	regs[BMI160_INT_STATUS_3] = 0x00;
	regs[BMI160_TEMPERATURE_0] = 0x00;
	regs[BMI160_TEMPERATURE_1] = 0x00;
	regs[BMI160_FIFO_LENGTH_0] = 0x00;
	regs[BMI160_FIFO_LENGTH_1] = 0x00;
	regs[BMI160_FIFO_DATA] = 0x00;
	regs[BMI160_ACC_CONF] = 0x28;
	regs[BMI160_ACC_RANGE] = 0x03;
	regs[BMI160_GYR_CONF] = 0x28;
	regs[BMI160_GYR_RANGE] = 0x00;
	regs[BMI160_MAG_CONF] = 0x0b;
	regs[BMI160_FIFO_DOWNS] = 0x88;
	regs[BMI160_FIFO_CONFIG_0] = 0x80;
	regs[BMI160_FIFO_CONFIG_1] = 0x10;
	regs[BMI160_MAG_IF_0] = 0x20;
	regs[BMI160_MAG_IF_1] = 0x80;
	regs[BMI160_MAG_IF_2] = 0x42;
	regs[BMI160_MAG_IF_3] = 0x4c;
	regs[BMI160_MAG_IF_4] = 0x00;
	regs[BMI160_INT_EN_0] = 0x00;
	regs[BMI160_INT_EN_1] = 0x00;
	regs[BMI160_INT_EN_2] = 0x00;
	regs[BMI160_INT_OUT_CTRL] = 0x00;
	regs[BMI160_INT_LATCH] = 0x00;
	regs[BMI160_INT_MAP_0] = 0x00;
	regs[BMI160_INT_MAP_1] = 0x00;
	regs[BMI160_INT_MAP_2] = 0x00;
	regs[BMI160_INT_DATA_0] = 0x00;
	regs[BMI160_INT_DATA_1] = 0x00;
	regs[BMI160_INT_LOW_HIGH_0] = 0x07;
	regs[BMI160_INT_LOW_HIGH_1] = 0x30;
	regs[BMI160_INT_LOW_HIGH_2] = 0x81;
	regs[BMI160_INT_LOW_HIGH_3] = 0xdb;
	regs[BMI160_INT_LOW_HIGH_4] = 0xc0;
	regs[BMI160_INT_MOTION_0] = 0x00;
	regs[BMI160_INT_MOTION_1] = 0x14;
	regs[BMI160_INT_MOTION_2] = 0x14;
	regs[BMI160_INT_MOTION_3] = 0x24;
	regs[BMI160_INT_TAP_0] = 0x04;
	regs[BMI160_INT_TAP_1] = 0xda;
	regs[BMI160_INT_ORIENT_0] = 0x18;
	regs[BMI160_INT_ORIENT_1] = 0x48;
	regs[BMI160_INT_FLAT_0] = 0x08;
	regs[BMI160_INT_FLAT_1] = 0x11;
	regs[BMI160_FOC_CONF] = 0x00;
	regs[BMI160_CONF] = 0x00;
	regs[BMI160_IF_CONF] = 0x00;
	regs[BMI160_PMU_TRIGGER] = 0x00;
	regs[BMI160_SELF_TEST] = 0x00;
	regs[BMI160_STEP_CNT_0] = 0x00;
	regs[BMI160_STEP_CNT_1] = 0x00;
	regs[BMI160_STEP_CONF_0] = 0x00;
	regs[BMI160_STEP_CONF_1] = 0x15;
	regs[BMI160_CMD_REG] = 0x03;

	/* Call generic reset */
	tag_time = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_TAG_TIME_EN;
	header = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_HEADER_EN;
	bmi_emul_reset_common(emul, tag_time, header);
}

/**
 * @brief Clear all interrupt registers
 *
 * @param regs Pointer to array of emulator's registers
 */
static void bmi160_emul_clear_int(uint8_t *regs)
{
	regs[BMI160_INT_STATUS_0] = 0x00;
	regs[BMI160_INT_STATUS_1] = 0x00;
	regs[BMI160_INT_STATUS_2] = 0x00;
	regs[BMI160_INT_STATUS_3] = 0x00;
}

/**
 * @brief Get offset value for given gyroscope value. If gyroscope value is
 *        above maximum (belowe minimum), then minimum -31,25°/s
 *        (maximum 31,25°/s) offset value is returned.
 *
 * @param gyr Gyroscope value
 */
static int16_t bmi160_emul_get_gyr_target_off(int32_t gyr)
{
	if (gyr > (int32_t)BMI_EMUL_125_DEG_S / 4) {
		return -((int32_t)BMI_EMUL_125_DEG_S / 4);
	}

	if (gyr < -((int32_t)BMI_EMUL_125_DEG_S / 4)) {
		return BMI_EMUL_125_DEG_S / 4;
	}

	return -gyr;
}

/**
 * @brief Get offset value for given accelerometer value. If accelerometer
 *        value - target is above maximum (belowe minimum), then minimum -0.5g
 *        (maximum 0.5g) offset value is returned.
 *
 * @param acc Accelerometer value
 * @param target Target value in FOC configuration register format
 */
static int16_t bmi160_emul_get_acc_target_off(int32_t acc, uint8_t target)
{
	switch (target) {
	case BMI160_FOC_ACC_PLUS_1G:
		acc -= BMI_EMUL_1G;
		break;
	case BMI160_FOC_ACC_MINUS_1G:
		acc += BMI_EMUL_1G;
		break;
	}

	if (acc > (int32_t)BMI_EMUL_1G / 2) {
		return -((int32_t)BMI_EMUL_1G / 2);
	}

	if (acc < -((int32_t)BMI_EMUL_1G / 2)) {
		return BMI_EMUL_1G / 2;
	}

	return -acc;
}

/**
 * @brief Handle fast offset compensation. Check FOC configuration register
 *        and sets gyroscope and/or accelerometer offset using current emulator
 *        state.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 */
static void bmi160_emul_handle_off_comp(uint8_t *regs, const struct emul *emul)
{
	uint8_t target;
	int16_t off;
	int32_t val;

	if (regs[BMI160_FOC_CONF] & BMI160_FOC_GYRO_EN) {
		val = bmi_emul_get_value(emul, BMI_EMUL_GYR_X);
		off = bmi160_emul_get_gyr_target_off(val);
		bmi_emul_set_off(emul, BMI_EMUL_GYR_X, off);
		val = bmi_emul_get_value(emul, BMI_EMUL_GYR_Y);
		off = bmi160_emul_get_gyr_target_off(val);

		bmi_emul_set_off(emul, BMI_EMUL_GYR_Y, off);
		val = bmi_emul_get_value(emul, BMI_EMUL_GYR_Z);
		off = bmi160_emul_get_gyr_target_off(val);
		bmi_emul_set_off(emul, BMI_EMUL_GYR_Z, off);
	}

	target = (regs[BMI160_FOC_CONF] >> BMI160_FOC_ACC_X_OFFSET) & 0x3;
	if (target) {
		val = bmi_emul_get_value(emul, BMI_EMUL_ACC_X);
		off = bmi160_emul_get_acc_target_off(val, target);
		bmi_emul_set_off(emul, BMI_EMUL_ACC_X, off);
	}

	target = (regs[BMI160_FOC_CONF] >> BMI160_FOC_ACC_Y_OFFSET) & 0x3;
	if (target) {
		val = bmi_emul_get_value(emul, BMI_EMUL_ACC_Y);
		off = bmi160_emul_get_acc_target_off(val, target);
		bmi_emul_set_off(emul, BMI_EMUL_ACC_Y, off);
	}

	target = (regs[BMI160_FOC_CONF] >> BMI160_FOC_ACC_Z_OFFSET) & 0x3;
	if (target) {
		val = bmi_emul_get_value(emul, BMI_EMUL_ACC_Z);
		off = bmi160_emul_get_acc_target_off(val, target);
		bmi_emul_set_off(emul, BMI_EMUL_ACC_Z, off);
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
static int bmi160_emul_start_cmd(uint8_t *regs, const struct emul *emul,
				 int cmd)
{
	int time;

	switch (cmd) {
	case BMI160_CMD_SOFT_RESET:
		time = 1;
		break;
	case BMI160_CMD_START_FOC:
		if ((regs[BMI160_FOC_CONF] & BMI160_FOC_GYRO_EN) &&
		    ((regs[BMI160_PMU_STATUS] &
		      (0x3 << BMI160_PMU_GYR_OFFSET)) !=
		     BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET)) {
			LOG_ERR("Starting gyroscope FOC in low power mode");
			return -EIO;
		}

		if ((regs[BMI160_FOC_CONF] & ~BMI160_FOC_GYRO_EN) &&
		    ((regs[BMI160_PMU_STATUS] &
		      (0x3 << BMI160_PMU_ACC_OFFSET)) !=
		     BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET)) {
			LOG_ERR("Starting accelerometer FOC in low power mode");
			return -EIO;
		}

		regs[BMI160_STATUS] &= ~BMI160_FOC_RDY;
		time = 250;
		break;
	case BMI160_CMD_ACC_MODE_SUSP:
	case BMI160_CMD_GYR_MODE_SUSP:
	case BMI160_CMD_MAG_MODE_SUSP:
		time = 0;
		break;
	/* Real hardware probably switch faster if not in suspend mode */
	case BMI160_CMD_ACC_MODE_NORMAL:
	case BMI160_CMD_ACC_MODE_LOWPOWER:
		time = 4;
		break;
	case BMI160_CMD_GYR_MODE_NORMAL:
	case BMI160_CMD_GYR_MODE_FAST_STARTUP:
		time = 80;
		break;
	case BMI160_CMD_MAG_MODE_NORMAL:
	case BMI160_CMD_MAG_MODE_LOWPOWER:
		time = 1;
		break;
	case BMI160_CMD_FIFO_FLUSH:
		time = 0;
		break;
	case BMI160_CMD_INT_RESET:
		time = 0;
		break;
	default:
		LOG_ERR("Unknown command 0x%x", cmd);
		return -EIO;
	}

	regs[BMI160_CMD_REG] = cmd;
	bmi_emul_set_cmd_end_time(emul, time);

	return 0;
}

/**
 * @brief Emulate end of ongoing command.
 *
 * @param regs Pointer to array of emulator's registers
 * @param emul Pointer to BMI emulator
 */
static void bmi160_emul_end_cmd(uint8_t *regs, const struct emul *emul)
{
	uint8_t pmu_status;
	bool tag_time;
	bool header;
	int cmd;

	pmu_status = regs[BMI160_PMU_STATUS];
	cmd = regs[BMI160_CMD_REG];
	regs[BMI160_CMD_REG] = BMI160_CMD_NOOP;
	tag_time = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_TAG_TIME_EN;
	header = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_HEADER_EN;

	switch (cmd) {
	case BMI160_CMD_SOFT_RESET:
		bmi160_emul_reset(regs, emul);
		break;
	case BMI160_CMD_START_FOC:
		bmi160_emul_handle_off_comp(regs, emul);
		regs[BMI160_STATUS] |= BMI160_FOC_RDY;
		break;
	case BMI160_CMD_ACC_MODE_SUSP:
		pmu_status &= ~(0x3 << BMI160_PMU_ACC_OFFSET);
		pmu_status |= BMI160_PMU_SUSPEND << BMI160_PMU_ACC_OFFSET;
		break;
	case BMI160_CMD_ACC_MODE_NORMAL:
		pmu_status &= ~(0x3 << BMI160_PMU_ACC_OFFSET);
		pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_ACC_OFFSET;
		break;
	case BMI160_CMD_ACC_MODE_LOWPOWER:
		pmu_status &= ~(0x3 << BMI160_PMU_ACC_OFFSET);
		pmu_status |= BMI160_PMU_LOW_POWER << BMI160_PMU_ACC_OFFSET;
		break;
	case BMI160_CMD_GYR_MODE_SUSP:
		pmu_status &= ~(0x3 << BMI160_PMU_GYR_OFFSET);
		pmu_status |= BMI160_PMU_SUSPEND << BMI160_PMU_GYR_OFFSET;
		break;
	case BMI160_CMD_GYR_MODE_NORMAL:
		pmu_status &= ~(0x3 << BMI160_PMU_GYR_OFFSET);
		pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_GYR_OFFSET;
		break;
	case BMI160_CMD_GYR_MODE_FAST_STARTUP:
		pmu_status &= ~(0x3 << BMI160_PMU_GYR_OFFSET);
		pmu_status |= BMI160_PMU_FAST_STARTUP << BMI160_PMU_GYR_OFFSET;
		break;
	case BMI160_CMD_MAG_MODE_SUSP:
		pmu_status &= ~(0x3 << BMI160_PMU_MAG_OFFSET);
		pmu_status |= BMI160_PMU_SUSPEND << BMI160_PMU_MAG_OFFSET;
		break;
	case BMI160_CMD_MAG_MODE_NORMAL:
		pmu_status &= ~(0x3 << BMI160_PMU_MAG_OFFSET);
		pmu_status |= BMI160_PMU_NORMAL << BMI160_PMU_MAG_OFFSET;
		break;
	case BMI160_CMD_MAG_MODE_LOWPOWER:
		pmu_status &= ~(0x3 << BMI160_PMU_MAG_OFFSET);
		pmu_status |= BMI160_PMU_LOW_POWER << BMI160_PMU_MAG_OFFSET;
		break;
	case BMI160_CMD_FIFO_FLUSH:
		bmi_emul_flush_fifo(emul, tag_time, header);
		break;
	case BMI160_CMD_INT_RESET:
		bmi160_emul_clear_int(regs);
		break;
	}

	/* Clear FIFO on sensor on/off in headerless mode */
	if (pmu_status != regs[BMI160_PMU_STATUS] && !header) {
		bmi_emul_flush_fifo(emul, tag_time, header);
	}

	regs[BMI160_PMU_STATUS] = pmu_status;
}

/**
 * @brief BMI160 specific write function. It doesn't handle block writes.
 *        Check if read only register is not accessed. Before writing value,
 *        ongoing command is finished if possible. Write to CMD register is
 *        handled by BMI160 specific function. On changing of FIFO
 *        header/headerless mode, FIFO is flushed.
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
static int bmi160_emul_handle_write(uint8_t *regs, const struct emul *emul,
				    int reg, int byte, uint8_t val)
{
	bool tag_time;
	bool header;

	if (byte > 1) {
		LOG_ERR("Block writes are not allowed");
		return -EIO;
	}

	if (reg <= BMI160_FIFO_DATA ||
	    (reg >= BMI160_STEP_CNT_0 && reg <= BMI160_STEP_CNT_1)) {
		return BMI_EMUL_ACCESS_E;
	}

	/* Stop on going command if required */
	if (regs[BMI160_CMD_REG] != BMI160_CMD_NOOP &&
	    bmi_emul_is_cmd_end(emul)) {
		bmi160_emul_end_cmd(regs, emul);
	}

	switch (reg) {
	case BMI160_CMD_REG:
		if (regs[BMI160_CMD_REG] != BMI160_CMD_NOOP) {
			LOG_ERR("Issued command before previous end");
			return -EIO;
		}

		return bmi160_emul_start_cmd(regs, emul, val);
	case BMI160_FIFO_CONFIG_1:
		tag_time = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_TAG_TIME_EN;
		header = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_HEADER_EN;
		/*
		 * Clear FIFO on transition between headerless and
		 * header mode
		 */
		if (!!(val & BMI160_FIFO_HEADER_EN) != header) {
			bmi_emul_flush_fifo(emul, tag_time, header);
		}
		break;
	}

	return 0;
}

/**
 * @brief Get currently accessed register. It is first register plus number of
 *        handled bytes for all registers except BMI160_FIFO_DATA for which
 *        address incrementation is disabled.
 *
 * @param emul Pointer to BMI emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int bmi160_emul_access_reg(const struct emul *emul, int reg, int byte,
				  bool read)
{
	if (!read) {
		return reg;
	}

	/*
	 * If register is FIFO data, then read data from FIFO.
	 * Else block read access subsequent registers.
	 */
	if (reg <= BMI160_FIFO_DATA && reg + byte >= BMI160_FIFO_DATA) {
		return BMI160_FIFO_DATA;
	}

	return reg + byte;
}

/**
 * @brief BMI160 specific read function. It handle block reads but only if
 *        device is not suspended. FIFO data register is trap register, so
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
static int bmi160_emul_handle_read(uint8_t *regs, const struct emul *emul,
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
	fifo_byte = byte - (reg - BMI160_FIFO_DATA);

	reg = bmi160_emul_access_reg(emul, reg, byte, true /* = read */);

	/* Stop on going command if required */
	if (regs[BMI160_CMD_REG] != BMI160_CMD_NOOP &&
	    bmi_emul_is_cmd_end(emul)) {
		bmi160_emul_end_cmd(regs, emul);
	}

	/* Burst reads are not supported if all sensors are in suspend mode */
	if ((regs[BMI160_PMU_STATUS] & 0x3f) == 0 && byte > 0) {
		LOG_ERR("Block reads are not supported in suspend mode");
		return -EIO;
	}

	tag_time = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_TAG_TIME_EN;
	header = regs[BMI160_FIFO_CONFIG_1] & BMI160_FIFO_HEADER_EN;
	acc_off_en = regs[BMI160_OFFSET_EN_GYR98] & BMI160_OFFSET_ACC_EN;
	gyr_off_en = regs[BMI160_OFFSET_EN_GYR98] & BMI160_OFFSET_GYRO_EN;
	gyr_shift = bmi160_emul_gyr_range_to_shift(regs[BMI160_GYR_RANGE]);
	acc_shift = bmi160_emul_acc_range_to_shift(regs[BMI160_ACC_RANGE]);

	switch (reg) {
	case BMI160_GYR_X_L_G:
	case BMI160_GYR_X_H_G:
	case BMI160_GYR_Y_L_G:
	case BMI160_GYR_Y_H_G:
	case BMI160_GYR_Z_L_G:
	case BMI160_GYR_Z_H_G:
	case BMI160_ACC_X_L_G:
	case BMI160_ACC_X_H_G:
	case BMI160_ACC_Y_L_G:
	case BMI160_ACC_Y_H_G:
	case BMI160_ACC_Z_L_G:
	case BMI160_ACC_Z_H_G:
	case BMI160_SENSORTIME_0:
	case BMI160_SENSORTIME_1:
	case BMI160_SENSORTIME_2:
		/*
		 * Snapshot of current emulator state is created on data read
		 * and shouldn't be changed until next I2C operation
		 */
		if (byte == 0) {
			bmi_emul_state_to_reg(emul, acc_shift, gyr_shift,
					      BMI160_ACC_X_L_G,
					      BMI160_GYR_X_L_G,
					      BMI160_SENSORTIME_0, acc_off_en,
					      gyr_off_en);
		}
		break;
	case BMI160_FIFO_LENGTH_0:
	case BMI160_FIFO_LENGTH_1:
		if (byte == 0) {
			fifo_len = bmi_emul_fifo_len(emul, tag_time, header);
			regs[BMI160_FIFO_LENGTH_0] = fifo_len & 0xff;
			regs[BMI160_FIFO_LENGTH_1] = (fifo_len >> 8) & 0x7;
		}
		break;
	case BMI160_FIFO_DATA:
		regs[reg] = bmi_emul_get_fifo_data(emul, fifo_byte, tag_time,
						   header, acc_shift,
						   gyr_shift);
		break;
	}

	*buf = regs[reg];

	return 0;
}

/** Registers backed in NVM by BMI160 */
const int bmi160_nvm_reg[] = {
	BMI160_NV_CONF,		 BMI160_OFFSET_ACC70,	BMI160_OFFSET_ACC70 + 1,
	BMI160_OFFSET_ACC70 + 2, BMI160_OFFSET_GYR70,	BMI160_OFFSET_GYR70 + 1,
	BMI160_OFFSET_GYR70 + 2, BMI160_OFFSET_EN_GYR98
};

/** Confguration of BMI160 */
struct bmi_emul_type_data bmi160_emul = {
	.sensortime_follow_config_frame = false,
	.start_write = NULL,
	.handle_write = bmi160_emul_handle_write,
	.finish_write = NULL,
	.start_read = NULL,
	.handle_read = bmi160_emul_handle_read,
	.finish_read = NULL,
	.access_reg = bmi160_emul_access_reg,
	.reset = bmi160_emul_reset,
	.rsvd_mask = bmi_emul_160_rsvd_mask,
	.nvm_reg = bmi160_nvm_reg,
	.nvm_len = ARRAY_SIZE(bmi160_nvm_reg),
	.gyr_off_reg = BMI160_OFFSET_GYR70,
	.acc_off_reg = BMI160_OFFSET_ACC70,
	.gyr98_off_reg = BMI160_OFFSET_EN_GYR98,
	.frame_order = { BMI_EMUL_FRAME_MAG, BMI_EMUL_FRAME_GYR,
			 BMI_EMUL_FRAME_ACC, BMI_EMUL_FRAME_NONE },
	.reg_bytes = 1,
};

/** Check description in emul_bmi.h */
const struct bmi_emul_type_data *get_bmi160_emul_type_data(void)
{
	return &bmi160_emul;
}
