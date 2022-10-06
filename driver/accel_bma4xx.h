/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMA4XX gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCEL_BMA4XX_H
#define __CROS_EC_ACCEL_BMA4XX_H

#define BMA4_I2C_ADDR_PRIMARY 0x18
#define BMA4_I2C_ADDR_SECONDARY 0x19
#define BMA4_I2C_BMM150_ADDR 0x10

/* Chip-specific registers */
#define BMA4_CHIP_ID_ADDR 0x00
#define BMA4_CHIP_ID_MIN 0x10
#define BMA4_CHIP_ID_MAX 0x15

#define BMA4_ERROR_ADDR 0x02
#define BMA4_FATAL_ERR_MSK 0x01
#define BMA4_CMD_ERR_POS 1
#define BMA4_CMD_ERR_MSK 0x02
#define BMA4_ERR_CODE_POS 2
#define BMA4_ERR_CODE_MSK 0x1C
#define BMA4_FIFO_ERR_POS 6
#define BMA4_FIFO_ERR_MSK 0x40
#define BMA4_AUX_ERR_POS 7
#define BMA4_AUX_ERR_MSK 0x80

#define BMA4_STATUS_ADDR 0x03
#define BMA4_STAT_DATA_RDY_ACCEL_POS 7
#define BMA4_STAT_DATA_RDY_ACCEL_MSK 0x80

#define BMA4_DATA_0_ADDR 0x0A
#define BMA4_DATA_8_ADDR 0x12

#define BMA4_SENSORTIME_0_ADDR 0x18
#define BMA4_INT_STAT_0_ADDR 0x1C
#define BMA4_INT_STAT_1_ADDR 0x1D
#define BMA4_STEP_CNT_OUT_0_ADDR 0x1E
#define BMA4_HIGH_G_OUT_ADDR 0x1F
#define BMA4_TEMPERATURE_ADDR 0x22

#define BMA4_FIFO_LENGTH_0_ADDR 0x24
#define BMA4_FIFO_DATA_ADDR 0x26
#define BMA4_ACTIVITY_OUT_ADDR 0x27
#define BMA4_ORIENTATION_OUT_ADDR 0x28

#define BMA4_INTERNAL_STAT 0x2A
#define BMA4_ASIC_INITIALIZED 0x01

#define BMA4_ACCEL_CONFIG_ADDR 0x40
#define BMA4_ACCEL_ODR_POS 0
#define BMA4_ACCEL_ODR_MSK 0x0F
#define BMA4_ACCEL_BW_POS 4
#define BMA4_ACCEL_BW_MSK 0x70
#define BMA4_ACCEL_PERFMODE_POS 7
#define BMA4_ACCEL_PERFMODE_MSK 0x80
#define BMA4_OUTPUT_DATA_RATE_0_78HZ 0x01
#define BMA4_OUTPUT_DATA_RATE_1_56HZ 0x02
#define BMA4_OUTPUT_DATA_RATE_3_12HZ 0x03
#define BMA4_OUTPUT_DATA_RATE_6_25HZ 0x04
#define BMA4_OUTPUT_DATA_RATE_12_5HZ 0x05
#define BMA4_OUTPUT_DATA_RATE_25HZ 0x06
#define BMA4_OUTPUT_DATA_RATE_50HZ 0x07
#define BMA4_OUTPUT_DATA_RATE_100HZ 0x08
#define BMA4_OUTPUT_DATA_RATE_200HZ 0x09
#define BMA4_OUTPUT_DATA_RATE_400HZ 0x0A
#define BMA4_OUTPUT_DATA_RATE_800HZ 0x0B
#define BMA4_OUTPUT_DATA_RATE_1600HZ 0x0C
#define BMA4_ACCEL_OSR4_AVG1 0
#define BMA4_ACCEL_OSR2_AVG2 1
#define BMA4_ACCEL_NORMAL_AVG4 2
#define BMA4_ACCEL_CIC_AVG8 3
#define BMA4_ACCEL_RES_AVG16 4
#define BMA4_ACCEL_RES_AVG32 5
#define BMA4_ACCEL_RES_AVG64 6
#define BMA4_ACCEL_RES_AVG128 7
#define BMA4_CIC_AVG_MODE 0
#define BMA4_CONTINUOUS_MODE 1

#define BMA4_ACCEL_RANGE_ADDR 0x41
#define BMA4_ACCEL_RANGE_POS 0
#define BMA4_ACCEL_RANGE_MSK 0x03
#define BMA4_ACCEL_RANGE_2G 0
#define BMA4_ACCEL_RANGE_4G 1
#define BMA4_ACCEL_RANGE_8G 2
#define BMA4_ACCEL_RANGE_16G 3

#define BMA4_RESERVED_REG_5B_ADDR 0x5B
#define BMA4_RESERVED_REG_5C_ADDR 0x5C
#define BMA4_FEATURE_CONFIG_ADDR 0x5E
#define BMA4_INTERNAL_ERROR 0x5F
#define BMA4_IF_CONFIG_ADDR 0x6B
#define BMA4_FOC_ACC_CONF_VAL 0xB7

#define BMA4_NV_CONFIG_ADDR 0x70
#define BMA4_NV_ACCEL_OFFSET_POS 3
#define BMA4_NV_ACCEL_OFFSET_MSK 0x08

#define BMA4_OFFSET_0_ADDR 0x71
#define BMA4_OFFSET_1_ADDR 0x72
#define BMA4_OFFSET_2_ADDR 0x73

#define BMA4_POWER_CONF_ADDR 0x7C
#define BMA4_ADVANCE_POWER_SAVE_POS 0
#define BMA4_ADVANCE_POWER_SAVE_MSK 0x01

#define BMA4_POWER_CTRL_ADDR 0x7D
#define BMA4_ACCEL_ENABLE_POS 2
#define BMA4_ACCEL_ENABLE_MSK 0x04
#define BMA4_ENABLE 0x01
#define BMA4_DISABLE 0x00

#define BMA4_CMD_ADDR 0x7E
#define BMA4_NVM_PROG 0xA0
#define BMA4_FIFO_FLUSH 0xB0
#define BMA4_SOFT_RESET 0xB6

/* Other definitions */
#define BMA4_X_AXIS 0
#define BMA4_Y_AXIS 1
#define BMA4_Z_AXIS 2

#define BMA4_12_BIT_RESOLUTION 12
#define BMA4_14_BIT_RESOLUTION 14
#define BMA4_16_BIT_RESOLUTION 16

/*
 * The max positive value of accel data is 0x07FF, equal to range(g)
 * So, in order to get +1g, divide the 0x07FF by range
 */
#define BMA4_ACC_DATA_PLUS_1G(range) (0x07FF / (range))

/* For offset registers 1LSB - 3.9mg */
#define BMA4_OFFSET_ACC_MULTI_MG (3900 * 1000)
#define BMA4_OFFSET_ACC_DIV_MG 1000000

#define BMA4_FOC_SAMPLE_LIMIT 32

/* Min and Max sampling frequency in mHz */
#define BMA4_ACCEL_MIN_FREQ 12500
#define BMA4_ACCEL_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(1600000, 6250)

#define BMA4_RANGE_TO_REG(_range)                              \
	((_range) < 8 ? BMA4_ACCEL_RANGE_2G + ((_range) / 4) : \
			BMA4_ACCEL_RANGE_8G + ((_range) / 16))

#define BMA4_REG_TO_RANGE(_reg)                        \
	((_reg) < BMA4_ACCEL_RANGE_8G ? 2 + (_reg)*2 : \
					8 + ((_reg)-BMA4_ACCEL_RANGE_8G) * 8)

#define BMA4_ODR_TO_REG(_odr)                                               \
	((_odr) < 125000 ?                                                  \
		 BMA4_OUTPUT_DATA_RATE_0_78HZ + __fls(((_odr)*10) / 7800) : \
		 BMA4_OUTPUT_DATA_RATE_25HZ + __fls((_odr) / 25000))

#define BMA4_REG_TO_ODR(_reg)                                           \
	((_reg) < BMA4_OUTPUT_DATA_RATE_25HZ ?                          \
		 (7800 << ((_reg)-BMA4_OUTPUT_DATA_RATE_0_78HZ)) / 10 : \
		 25000 << ((_reg)-BMA4_OUTPUT_DATA_RATE_25HZ))

extern const struct accelgyro_drv bma4_accel_drv;

#endif /* __CROS_EC_ACCEL_BMA4XX_H */
