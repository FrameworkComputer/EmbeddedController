/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMI160 accelerometer and gyro and BMM150 compass module for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_BMI160_H
#define __CROS_EC_ACCELGYRO_BMI160_H

#include "accelgyro.h"
#include "driver/accelgyro_bmi160_public.h"
#include "mag_bmm150.h"

#define BMI160_CHIP_ID 0x00
#define BMI160_CHIP_ID_MAJOR 0xd1
#define BMI168_CHIP_ID_MAJOR 0xd2

#define BMI160_SPEC_ACC_STARTUP_TIME_MS 10
#define BMI160_SPEC_GYR_STARTUP_TIME_MS 80
#define BMI160_SPEC_MAG_STARTUP_TIME_MS 60

#define BMI160_ERR_REG 0x02
#define BMI160_PMU_STATUS 0x03
#define BMI160_PMU_MAG_OFFSET 0
#define BMI160_PMU_GYR_OFFSET 2
#define BMI160_PMU_ACC_OFFSET 4
#define BMI160_PMU_SENSOR_STATUS(_sensor_type, _val) \
	(((_val) >> (4 - 2 * (_sensor_type))) & 0x3)
#define BMI160_PMU_SUSPEND 0
#define BMI160_PMU_NORMAL 1
#define BMI160_PMU_LOW_POWER 2
#define BMI160_PMU_FAST_STARTUP 3

#define BMI160_MAG_X_L_G 0x04
#define BMI160_MAG_X_H_G 0x05
#define BMI160_MAG_Y_L_G 0x06
#define BMI160_MAG_Y_H_G 0x07
#define BMI160_MAG_Z_L_G 0x08
#define BMI160_MAG_Z_H_G 0x09
#define BMI160_RHALL_L_G 0x0a
#define BMI160_RHALL_H_G 0x0b
#define BMI160_GYR_X_L_G 0x0c
#define BMI160_GYR_X_H_G 0x0d
#define BMI160_GYR_Y_L_G 0x0e
#define BMI160_GYR_Y_H_G 0x0f
#define BMI160_GYR_Z_L_G 0x10
#define BMI160_GYR_Z_H_G 0x11
#define BMI160_ACC_X_L_G 0x12
#define BMI160_ACC_X_H_G 0x13
#define BMI160_ACC_Y_L_G 0x14
#define BMI160_ACC_Y_H_G 0x15
#define BMI160_ACC_Z_L_G 0x16
#define BMI160_ACC_Z_H_G 0x17

#define BMI160_SENSORTIME_0 0x18
#define BMI160_SENSORTIME_1 0x19
#define BMI160_SENSORTIME_2 0x1a

#define BMI160_STATUS 0x1b
#define BMI160_POR_DETECTED BIT(0)
#define BMI160_GYR_SLF_TST BIT(1)
#define BMI160_MAG_MAN_OP BIT(2)
#define BMI160_FOC_RDY BIT(3)
#define BMI160_NVM_RDY BIT(4)
#define BMI160_DRDY_MAG BIT(5)
#define BMI160_DRDY_GYR BIT(6)
#define BMI160_DRDY_ACC BIT(7)
#define BMI160_DRDY_OFF(_sensor) (7 - (_sensor))
#define BMI160_DRDY_MASK(_sensor) (1 << BMI160_DRDY_OFF(_sensor))

/* first 2 bytes are the interrupt reasons, next 2 some qualifier */
#define BMI160_INT_STATUS_0 0x1c
#define BMI160_STEP_INT BIT(0)
#define BMI160_SIGMOT_INT BIT(1)
#define BMI160_ANYM_INT BIT(2)
#define BMI160_PMU_TRIGGER_INT BIT(3)
#define BMI160_D_TAP_INT BIT(4)
#define BMI160_S_TAP_INT BIT(5)
#define BMI160_ORIENT_INT BIT(6)
#define BMI160_FLAT_INT BIT(7)
#define BMI160_ORIENT_XY_MASK 0x30
#define BMI160_ORIENT_PORTRAIT (0 << 4)
#define BMI160_ORIENT_PORTRAIT_INVERT BIT(4)
#define BMI160_ORIENT_LANDSCAPE (2 << 4)
#define BMI160_ORIENT_LANDSCAPE_INVERT (3 << 4)

#define BMI160_INT_STATUS_1 0x1d
#define BMI160_HIGHG_INT (1 << (2 + 8))
#define BMI160_LOWG_INT (1 << (3 + 8))
#define BMI160_DRDY_INT (1 << (4 + 8))
#define BMI160_FFULL_INT (1 << (5 + 8))
#define BMI160_FWM_INT (1 << (6 + 8))
#define BMI160_NOMO_INT (1 << (7 + 8))

#define BMI160_INT_MASK 0xFFFF

#define BMI160_INT_STATUS_2 0x1e
#define BMI160_INT_STATUS_3 0x1f
#define BMI160_FIRST_X (1 << (0 + 16))
#define BMI160_FIRST_Y (1 << (1 + 16))
#define BMI160_FIRST_Z (1 << (2 + 16))
#define BMI160_SIGN (1 << (3 + 16))
#define BMI160_ANYM_OFFSET 0
#define BMI160_TAP_OFFSET 4
#define BMI160_HIGH_OFFSET 8
#define BMI160_INT_INFO(_type, _data) \
	(CONCAT2(BMI160_, _data) << CONCAT3(BMI160_, _type, _OFFSET))

#define BMI160_ORIENT_Z (1 << (6 + 24))
#define BMI160_FLAT (1 << (7 + 24))

#define BMI160_TEMPERATURE_0 0x20
#define BMI160_TEMPERATURE_1 0x21

#define BMI160_FIFO_LENGTH_0 0x22
#define BMI160_FIFO_LENGTH_1 0x23
#define BMI160_FIFO_LENGTH_MASK (BIT(11) - 1)
#define BMI160_FIFO_DATA 0x24

#define BMI160_ACC_CONF 0x40
#define BMI160_ACC_BW_OFFSET 4
#define BMI160_ACC_BW_MASK (0x7 << BMI160_ACC_BW_OFFSET)

#define BMI160_ACC_RANGE 0x41
#define BMI160_GSEL_2G 0x03
#define BMI160_GSEL_4G 0x05
#define BMI160_GSEL_8G 0x08
#define BMI160_GSEL_16G 0x0c

#define BMI160_GYR_CONF 0x42
#define BMI160_GYR_BW_OFFSET 4
#define BMI160_GYR_BW_MASK (0x3 << BMI160_GYR_BW_OFFSET)

#define BMI160_GYR_RANGE 0x43
#define BMI160_DPS_SEL_2000 0x00
#define BMI160_DPS_SEL_1000 0x01
#define BMI160_DPS_SEL_500 0x02
#define BMI160_DPS_SEL_250 0x03
#define BMI160_DPS_SEL_125 0x04

#define BMI160_MAG_CONF 0x44

#define BMI160_FIFO_DOWNS 0x45
#define BMI160_FIFO_CONFIG_0 0x46
#define BMI160_FIFO_CONFIG_1 0x47
#define BMI160_FIFO_TAG_TIME_EN BIT(1)
#define BMI160_FIFO_TAG_INT2_EN BIT(2)
#define BMI160_FIFO_TAG_INT1_EN BIT(3)
#define BMI160_FIFO_HEADER_EN BIT(4)
#define BMI160_FIFO_MAG_EN BIT(5)
#define BMI160_FIFO_ACC_EN BIT(6)
#define BMI160_FIFO_GYR_EN BIT(7)
#define BMI160_FIFO_TARG_INT(_i) CONCAT3(BMI160_FIFO_TAG_INT, _i, _EN)
#define BMI160_FIFO_SENSOR_EN(_sensor)                                      \
	((_sensor) == MOTIONSENSE_TYPE_ACCEL ?                              \
		 BMI160_FIFO_ACC_EN :                                       \
		 ((_sensor) == MOTIONSENSE_TYPE_GYRO ? BMI160_FIFO_GYR_EN : \
						       BMI160_FIFO_MAG_EN))

#define BMI160_MAG_IF_0 0x4b
#define BMI160_MAG_I2C_ADDRESS BMI160_MAG_IF_0
#define BMI160_MAG_IF_1 0x4c
#define BMI160_MAG_I2C_CONTROL BMI160_MAG_IF_1
#define BMI160_MAG_READ_BURST_MASK 3
#define BMI160_MAG_READ_BURST_1 0
#define BMI160_MAG_READ_BURST_2 1
#define BMI160_MAG_READ_BURST_6 2
#define BMI160_MAG_READ_BURST_8 3
#define BMI160_MAG_OFFSET_OFF 3
#define BMI160_MAG_OFFSET_MASK (0xf << BMI160_MAG_OFFSET_OFF)
#define BMI160_MAG_MANUAL_EN BIT(7)

#define BMI160_MAG_IF_2 0x4d
#define BMI160_MAG_I2C_READ_ADDR BMI160_MAG_IF_2
#define BMI160_MAG_IF_3 0x4e
#define BMI160_MAG_I2C_WRITE_ADDR BMI160_MAG_IF_3
#define BMI160_MAG_IF_4 0x4f
#define BMI160_MAG_I2C_WRITE_DATA BMI160_MAG_IF_4
#define BMI160_MAG_I2C_READ_DATA BMI160_MAG_X_L_G

#define BMI160_INT_EN_0 0x50
#define BMI160_INT_ANYMO_X_EN BIT(0)
#define BMI160_INT_ANYMO_Y_EN BIT(1)
#define BMI160_INT_ANYMO_Z_EN BIT(2)
#define BMI160_INT_D_TAP_EN BIT(4)
#define BMI160_INT_S_TAP_EN BIT(5)
#define BMI160_INT_ORIENT_EN BIT(6)
#define BMI160_INT_FLAT_EN BIT(7)
#define BMI160_INT_EN_1 0x51
#define BMI160_INT_HIGHG_X_EN BIT(0)
#define BMI160_INT_HIGHG_Y_EN BIT(1)
#define BMI160_INT_HIGHG_Z_EN BIT(2)
#define BMI160_INT_LOW_EN BIT(3)
#define BMI160_INT_DRDY_EN BIT(4)
#define BMI160_INT_FFUL_EN BIT(5)
#define BMI160_INT_FWM_EN BIT(6)
#define BMI160_INT_EN_2 0x52
#define BMI160_INT_NOMOX_EN BIT(0)
#define BMI160_INT_NOMOY_EN BIT(1)
#define BMI160_INT_NOMOZ_EN BIT(2)
#define BMI160_INT_STEP_DET_EN BIT(3)

#define BMI160_INT_OUT_CTRL 0x53
#define BMI160_INT_EDGE_CTRL BIT(0)
#define BMI160_INT_LVL_CTRL BIT(1)
#define BMI160_INT_OD BIT(2)
#define BMI160_INT_OUTPUT_EN BIT(3)
#define BMI160_INT1_CTRL_OFFSET 0
#define BMI160_INT2_CTRL_OFFSET 4
#define BMI160_INT_CTRL(_i, _bit) \
	(CONCAT2(BMI160_INT_, _bit) << CONCAT3(BMI160_INT, _i, _CTRL_OFFSET))

#define BMI160_INT_LATCH 0x54
#define BMI160_INT1_INPUT_EN BIT(4)
#define BMI160_INT2_INPUT_EN BIT(5)
#define BMI160_LATCH_MASK 0xf
#define BMI160_LATCH_NONE 0
#define BMI160_LATCH_5MS 5
#define BMI160_LATCH_FOREVER 0xf

#define BMI160_INT_MAP_0 0x55
#define BMI160_INT_LOWG_STEP BIT(0)
#define BMI160_INT_HIGHG BIT(1)
#define BMI160_INT_ANYMOTION BIT(2)
#define BMI160_INT_NOMOTION BIT(3)
#define BMI160_INT_D_TAP BIT(4)
#define BMI160_INT_S_TAP BIT(5)
#define BMI160_INT_ORIENT BIT(6)
#define BMI160_INT_FLAT BIT(7)

#define BMI160_INT_MAP_1 0x56
#define BMI160_INT_PMU_TRIG BIT(0)
#define BMI160_INT_FFULL BIT(1)
#define BMI160_INT_FWM BIT(2)
#define BMI160_INT_DRDY BIT(3)
#define BMI160_INT1_MAP_OFFSET 4
#define BMI160_INT2_MAP_OFFSET 0
#define BMI160_INT_MAP(_i, _bit) \
	(CONCAT2(BMI160_INT_, _bit) << CONCAT3(BMI160_INT, _i, _MAP_OFFSET))
#define BMI160_INT_FIFO_MAP BMI160_INT_MAP_1

#define BMI160_INT_MAP_2 0x57

#define BMI160_INT_MAP_INT_1 BMI160_INT_MAP_0
#define BMI160_INT_MAP_INT_2 BMI160_INT_MAP_2
#define BMI160_INT_MAP_REG(_i) CONCAT2(BMI160_INT_MAP_INT_, _i)

#define BMI160_INT_DATA_0 0x58
#define BMI160_INT_DATA_1 0x59

#define BMI160_INT_LOW_HIGH_0 0x5a
#define BMI160_INT_LOW_HIGH_1 0x5b
#define BMI160_INT_LOW_HIGH_2 0x5c
#define BMI160_INT_LOW_HIGH_3 0x5d
#define BMI160_INT_LOW_HIGH_4 0x5e

#define BMI160_INT_MOTION_0 0x5f
#define BMI160_INT_MOTION_1 0x60
/*
 * The formula is defined in 2.11.25 (any motion interrupt [1]).
 *
 * if we want threshold at a (in mg), the register should be x, where
 * x * 7.81mg = a, assuming a range of 4G, which is
 * x * 4 * 1.953 = a so
 * x = a * 1000 / range * 1953
 */
#define BMI160_MOTION_TH(_s, _mg) \
	(MIN(((_mg) * 1000) / ((_s)->current_range * 1953), 0xff))
#define BMI160_INT_MOTION_2 0x61
#define BMI160_INT_MOTION_3 0x62
#define BMI160_MOTION_NO_MOT_SEL BIT(0)
#define BMI160_MOTION_SIG_MOT_SEL BIT(1)
#define BMI160_MOTION_SKIP_OFF 2
#define BMI160_MOTION_SKIP_MASK 0x3
#define BMI160_MOTION_SKIP_TIME(_ms) \
	(MIN(__fls((_ms) / 1500), BMI160_MOTION_SKIP_MASK))
#define BMI160_MOTION_PROOF_OFF 4
#define BMI160_MOTION_PROOF_MASK 0x3
#define BMI160_MOTION_PROOF_TIME(_ms) \
	(MIN(__fls((_ms) / 250), BMI160_MOTION_PROOF_MASK))

#define BMI160_INT_TAP_0 0x63
#define BMI160_TAP_DUR(_s, _ms)                   \
	((_ms) <= 250 ? MAX((_ms), 50) / 50 - 1 : \
	 (_ms) <= 500 ? 4 + ((_ms)-250) / 125 :   \
	 (_ms) < 700  ? 6 :                       \
			7)

#define BMI160_INT_TAP_1 0x64
#define BMI160_TAP_TH(_s, _mg) \
	(MIN(((_mg) * 1000) / ((_s)->current_range * 31250), 0x1f))

#define BMI160_INT_ORIENT_0 0x65

/* No hysterisis, theta block, int on slope > 0.2 or axis > 1.5, symmetrical */
#define BMI160_INT_ORIENT_0_INIT_VAL 0x48

#define BMI160_INT_ORIENT_1 0x66

/* no axes remap, no int on up/down, no blocking angle */
#define BMI160_INT_ORIENT_1_INIT_VAL 0x00

#define BMI160_INT_FLAT_0 0x67
#define BMI160_INT_FLAT_1 0x68

#define BMI160_FOC_CONF 0x69
#define BMI160_FOC_GYRO_EN BIT(6)
#define BMI160_FOC_ACC_PLUS_1G 1
#define BMI160_FOC_ACC_MINUS_1G 2
#define BMI160_FOC_ACC_0G 3
#define BMI160_FOC_ACC_Z_OFFSET 0
#define BMI160_FOC_ACC_Y_OFFSET 2
#define BMI160_FOC_ACC_X_OFFSET 4

#define BMI160_CONF 0x6a
#define BMI160_IF_CONF 0x6b
#define BMI160_IF_MODE_OFF 4
#define BMI160_IF_MODE_MASK 3
#define BMI160_IF_MODE_AUTO_OFF 0
#define BMI160_IF_MODE_I2C_IOS 1
#define BMI160_IF_MODE_AUTO_I2C 2

#define BMI160_PMU_TRIGGER 0x6c
#define BMI160_SELF_TEST 0x6d

#define BMI160_NV_CONF 0x70

#define BMI160_OFFSET_ACC70 0x71
#define BMI160_OFFSET_GYR70 0x74
#define BMI160_OFFSET_EN_GYR98 0x77
#define BMI160_OFFSET_ACC_EN BIT(6)
#define BMI160_OFFSET_GYRO_EN BIT(7)

#define BMI160_STEP_CNT_0 0x78
#define BMI160_STEP_CNT_1 0x79
#define BMI160_STEP_CONF_0 0x7a
#define BMI160_STEP_CONF_1 0x7b

#define BMI160_CMD_REG 0x7e
#define BMI160_CMD_SOFT_RESET 0xb6
#define BMI160_CMD_NOOP 0x00
#define BMI160_CMD_START_FOC 0x03
#define BMI160_CMD_ACC_MODE_OFFSET 0x10
#define BMI160_CMD_ACC_MODE_SUSP 0x10
#define BMI160_CMD_ACC_MODE_NORMAL 0x11
#define BMI160_CMD_ACC_MODE_LOWPOWER 0x12
#define BMI160_CMD_GYR_MODE_SUSP 0x14
#define BMI160_CMD_GYR_MODE_NORMAL 0x15
#define BMI160_CMD_GYR_MODE_FAST_STARTUP 0x17
#define BMI160_CMD_MAG_MODE_SUSP 0x18
#define BMI160_CMD_MAG_MODE_NORMAL 0x19
#define BMI160_CMD_MAG_MODE_LOWPOWER 0x1a
#define BMI160_CMD_MODE_SUSPEND(_sensor_type) \
	(BMI160_CMD_ACC_MODE_OFFSET | (_sensor_type) << 2 | BMI160_PMU_SUSPEND)
#define BMI160_CMD_MODE_NORMAL(_sensor_type) \
	(BMI160_CMD_ACC_MODE_OFFSET | (_sensor_type) << 2 | BMI160_PMU_NORMAL)

#define BMI160_CMD_FIFO_FLUSH 0xb0
#define BMI160_CMD_INT_RESET 0xb1
#define BMI160_CMD_SOFT_RESET 0xb6
#define BMI160_CMD_EXT_MODE_EN_B0 0x37
#define BMI160_CMD_EXT_MODE_EN_B1 0x9a
#define BMI160_CMD_EXT_MODE_EN_B2 0xc0

#define BMI160_CMD_EXT_MODE_ADDR 0x7f
#define BMI160_CMD_PAGING_EN BIT(7)
#define BMI160_CMD_TARGET_PAGE BIT(4)
#define BMI160_COM_C_TRIM_ADDR 0x85
#define BMI160_COM_C_TRIM (3 << 4)

#define BMI160_CMD_TGT_PAGE 0
#define BMI160_CMD_TGT_PAGE_COM 1
#define BMI160_CMD_TGT_PAGE_ACC 2
#define BMI160_CMD_TGT_PAGE_GYR 3

#define BMI160_FF_FRAME_LEN_TS 4
#define BMI160_FF_DATA_LEN_ACC 6
#define BMI160_FF_DATA_LEN_GYR 6
#define BMI160_FF_DATA_LEN_MAG 8

/* Root mean square noise of 100 Hz accelerometer, units: ug */
#define BMI160_ACCEL_RMS_NOISE_100HZ 1300

/* Functions to access the secondary device through the accel/gyro. */
int bmi160_sec_raw_read8(const int port, const uint16_t addr_flags,
			 const uint8_t reg, int *data_ptr);
int bmi160_sec_raw_write8(const int port, const uint16_t addr_flags,
			  const uint8_t reg, int data);

#if defined(CONFIG_ZEPHYR)
#if DT_NODE_EXISTS(DT_ALIAS(bmi160_int))
/* Get the motion sensor ID of the BMI160 sensor that generates the interrupt.
 * The interrupt is converted to the event and transferred to motion sense task
 * that actually handles the interrupt.
 *
 * Here we use an alias (bmi160_int) to get the motion sensor ID. This alias
 * MUST be defined for this driver to work.
 * aliases {
 *   bmi160-int = &base_accel;
 * };
 */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(bmi160_int)))
#endif
#endif /* CONFIG_ZEPHYR */

#endif /* __CROS_EC_ACCELGYRO_BMI160_H */
