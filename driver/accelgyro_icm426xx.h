/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ICM-426xx accelerometer and gyroscope for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_ICM426XX_H
#define __CROS_EC_ACCELGYRO_ICM426XX_H

#include "accelgyro.h"
#include "common.h"

/*
 * 7-bit address is 110100Xb. Where 'X' is determined
 * by the logic level on pin AP_AD0.
 */
#define ICM426XX_ADDR0_FLAGS 0x68
#define ICM426XX_ADDR1_FLAGS 0x69

/* Min and Max sampling frequency in mHz */
#define ICM426XX_ACCEL_MIN_FREQ 3125
#define ICM426XX_ACCEL_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(500000, 100000)
#define ICM426XX_GYRO_MIN_FREQ 12500
#define ICM426XX_GYRO_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(4000000, 100000)

/* Min and Max Accel FS in G */
#define ICM426XX_ACCEL_FS_MIN_VAL 2
#define ICM426XX_ACCEL_FS_MAX_VAL 16

/* Min and Max Gyro FS in dps */
#define ICM426XX_GYRO_FS_MIN_VAL 125
#define ICM426XX_GYRO_FS_MAX_VAL 2000

/* accel stabilization time in us */
#define ICM426XX_ACCEL_START_TIME 20000
#define ICM426XX_ACCEL_STOP_TIME 0

/* gyro stabilization time in us */
#define ICM426XX_GYRO_START_TIME 60000
#define ICM426XX_GYRO_STOP_TIME 150000

/* Reg value from Accel FS in G */
#define ICM426XX_ACCEL_FS_TO_REG(_fs) \
	((_fs) < 2 ? 3 : (_fs) > 16 ? 0 : 3 - __fls((_fs) / 2))

/* Accel FSR in G from Reg value */
#define ICM426XX_ACCEL_REG_TO_FS(_reg) ((1 << (3 - (_reg))) * 2)

/* Reg value from Gyro FS in dps */
#define ICM426XX_GYRO_FS_TO_REG(_fs) \
	((_fs) < 125 ? 4 : (_fs) > 2000 ? 0 : 4 - __fls((_fs) / 125))

/* Gyro FSR in dps from Reg value */
#define ICM426XX_GYRO_REG_TO_FS(_reg) ((1 << (4 - (_reg))) * 125)

/* Reg value from ODR in mHz */
#define ICM426XX_ODR_TO_REG(_odr)                       \
	((_odr) <= 200000 ? 13 - __fls((_odr) / 3125) : \
	 (_odr) < 500000  ? 7 :                         \
	 (_odr) < 1000000 ? 15 :                        \
			    6 - __fls((_odr) / 1000000))

/* ODR in mHz from Reg value */
#define ICM426XX_REG_TO_ODR(_reg)                     \
	((_reg) == 15 ? 500000 :                      \
	 (_reg) >= 7  ? (1 << (13 - (_reg))) * 3125 : \
			(1 << (6 - (_reg))) * 1000000)

/* Reg value for the next higher ODR */
#define ICM426XX_ODR_REG_UP(_reg) \
	((_reg) == 15 ? 6 : (_reg) == 7 ? 15 : (_reg)-1)

/*
 * Register addresses are virtual address on 16 bits.
 * MSB is coding register bank and LSB real register address.
 * ex: bank 4, register 1F => 0x041F
 */
#define ICM426XX_REG_DEVICE_CONFIG 0x0011
#define ICM426XX_SOFT_RESET_CONFIG BIT(0)

enum icm426xx_slew_rate {
	ICM426XX_SLEW_RATE_20NS_60NS,
	ICM426XX_SLEW_RATE_12NS_36NS,
	ICM426XX_SLEW_RATE_6NS_18NS,
	ICM426XX_SLEW_RATE_4NS_12NS,
	ICM426XX_SLEW_RATE_2NS_6NS,
	ICM426XX_SLEW_RATE_INF_2NS,
};
#define ICM426XX_REG_DRIVE_CONFIG 0x0013
#define ICM426XX_DRIVE_CONFIG_MASK GENMASK(5, 0)
#define ICM426XX_I2C_SLEW_RATE(_s) (((_s) & 0x07) << 3)
#define ICM426XX_SPI_SLEW_RATE(_s) ((_s) & 0x07)

/* default int configuration is pulsed mode, open drain, and active low */
#define ICM426XX_REG_INT_CONFIG 0x0014
#define ICM426XX_INT2_LATCHED BIT(5)
#define ICM426XX_INT2_PUSH_PULL BIT(4)
#define ICM426XX_INT2_ACTIVE_HIGH BIT(3)
#define ICM426XX_INT1_LATCHED BIT(2)
#define ICM426XX_INT1_PUSH_PULL BIT(1)
#define ICM426XX_INT1_ACTIVE_HIGH BIT(0)

#define ICM426XX_REG_FIFO_CONFIG 0x0016
#define ICM426XX_FIFO_MODE_BYPASS (0x00 << 6)
#define ICM426XX_FIFO_MODE_STREAM (0x01 << 6)
#define ICM426XX_FIFO_MODE_STOP_FULL (0x02 << 6)

/* data are 16 bits */
#define ICM426XX_REG_TEMP_DATA 0x001D
/* X + Y + Z: 3 * 16 bits */
#define ICM426XX_REG_ACCEL_DATA_XYZ 0x001F
#define ICM426XX_REG_GYRO_DATA_XYZ 0x0025

#define ICM426XX_INVALID_DATA -32768

#define ICM426XX_REG_INT_STATUS 0x002D
#define ICM426XX_UI_FSYNC_INT BIT(6)
#define ICM426XX_PLL_RDY_INT BIT(5)
#define ICM426XX_RESET_DONE_INT BIT(4)
#define ICM426XX_DATA_RDY_INT BIT(3)
#define ICM426XX_FIFO_THS_INT BIT(2)
#define ICM426XX_FIFO_FULL_INT BIT(1)
#define ICM426XX_AGC_RDY_INT BIT(0)

/* FIFO count is 16 bits */
#define ICM426XX_REG_FIFO_COUNT 0x002E
#define ICM426XX_REG_FIFO_DATA 0x0030

#define ICM426XX_REG_SIGNAL_PATH_RESET 0x004B
#define ICM426XX_ABORT_AND_RESET BIT(3)
#define ICM426XX_TMST_STROBE BIT(2)
#define ICM426XX_FIFO_FLUSH BIT(1)

#define ICM426XX_REG_INTF_CONFIG0 0x004C
#define ICM426XX_DATA_CONF_MASK GENMASK(7, 4)
#define ICM426XX_FIFO_HOLD_LAST_DATA BIT(7)
#define ICM426XX_FIFO_COUNT_REC BIT(6)
#define ICM426XX_FIFO_COUNT_BE BIT(5)
#define ICM426XX_SENSOR_DATA_BE BIT(4)
#define ICM426XX_UI_SIFS_CFG_MASK GENMASK(1, 0)
#define ICM426XX_UI_SIFS_CFG_SPI_DIS 0x02
#define ICM426XX_UI_SIFS_CFG_I2C_DIS 0x03

#define ICM426XX_REG_INTF_CONFIG1 0x004D
#define ICM426XX_ACCEL_LP_CLK_SEL BIT(3)

enum icm426xx_sensor_mode {
	ICM426XX_MODE_OFF,
	ICM426XX_MODE_STANDBY,
	ICM426XX_MODE_LOW_POWER,
	ICM426XX_MODE_LOW_NOISE,
};
#define ICM426XX_REG_PWR_MGMT0 0x004E
#define ICM426XX_TEMP_DIS BIT(5)
#define ICM426XX_IDLE BIT(4)
#define ICM426XX_GYRO_MODE_MASK GENMASK(3, 2)
#define ICM426XX_GYRO_MODE(_m) (((_m) & 0x03) << 2)
#define ICM426XX_ACCEL_MODE_MASK GENMASK(1, 0)
#define ICM426XX_ACCEL_MODE(_m) ((_m) & 0x03)

#define ICM426XX_REG_GYRO_CONFIG0 0x004F
#define ICM426XX_REG_ACCEL_CONFIG0 0x0050
#define ICM426XX_FS_MASK GENMASK(7, 5)
#define ICM426XX_FS_SEL(_fs) (((_fs) & 0x07) << 5)
#define ICM426XX_ODR_MASK GENMASK(3, 0)
#define ICM426XX_ODR(_odr) ((_odr) & 0x0F)

enum icm426xx_filter_bw {
	/* low noise mode */
	ICM426XX_FILTER_BW_ODR_DIV_2 = 0,

	/* low power mode */
	ICM426XX_FILTER_BW_AVG_1X = 1,
	ICM426XX_FILTER_BW_AVG_16X = 6,
};

#define ICM426XX_REG_GYRO_ACCEL_CONFIG0 0x0052
#define ICM426XX_ACCEL_UI_FILT_MASK GENMASK(7, 4)
#define ICM426XX_ACCEL_UI_FILT_BW(_f) (((_f) & 0x0F) << 4)
#define ICM426XX_GYRO_UI_FILT_MASK GENMASK(3, 0)
#define ICM426XX_GYRO_UI_FILT_BW(_f) ((_f) & 0x0F)

#define ICM426XX_REG_FIFO_CONFIG1 0x005F
#define ICM426XX_FIFO_PARTIAL_READ BIT(6)
#define ICM426XX_FIFO_WM_GT_TH BIT(5)
#define ICM426XX_FIFO_EN_MASK GENMASK(3, 0)
#define ICM426XX_FIFO_TMST_FSYNC_EN BIT(3)
#define ICM426XX_FIFO_TEMP_EN BIT(2)
#define ICM426XX_FIFO_GYRO_EN BIT(1)
#define ICM426XX_FIFO_ACCEL_EN BIT(0)

/* FIFO watermark value is 16 bits little endian */
#define ICM426XX_REG_FIFO_WATERMARK 0x0060

#define ICM426XX_REG_INT_CONFIG1 0x0064
#define ICM426XX_INT_PULSE_DURATION BIT(6)
#define ICM426XX_INT_TDEASSERT_DIS BIT(5)
#define ICM426XX_INT_ASYNC_RESET BIT(4)

#define ICM426XX_REG_INT_SOURCE0 0x0065
#define ICM426XX_UI_FSYNC_INT1_EN BIT(6)
#define ICM426XX_PLL_RDY_INT1_EN BIT(5)
#define ICM426XX_RESET_DONE_INT1_EN BIT(4)
#define ICM426XX_UI_DRDY_INT1_EN BIT(3)
#define ICM426XX_FIFO_THS_INT1_EN BIT(2)
#define ICM426XX_FIFO_FULL_INT1_EN BIT(1)
#define ICM426XX_UI_AGC_RDY_INT1_EN BIT(0)

#define ICM426XX_REG_INT_SOURCE3 0x0068
#define ICM426XX_UI_FSYNC_INT2_EN BIT(6)
#define ICM426XX_PLL_RDY_INT2_EN BIT(5)
#define ICM426XX_RESET_DONE_INT2_EN BIT(4)
#define ICM426XX_UI_DRDY_INT2_EN BIT(3)
#define ICM426XX_FIFO_THS_INT2_EN BIT(2)
#define ICM426XX_FIFO_FULL_INT2_EN BIT(1)
#define ICM426XX_UI_AGC_RDY_INT2_EN BIT(0)

#define ICM426XX_REG_WHO_AM_I 0x0075
#define ICM426XX_CHIP_ICM40608 0x39
#define ICM426XX_CHIP_ICM42605 0x42

#define ICM426XX_REG_BANK_SEL 0x0076
#define ICM426XX_BANK_SEL(_b) ((_b) & 0x07)

#define ICM426XX_REG_INTF_CONFIG4 0x017A
#define ICM426XX_I3C_BUS_MODE BIT(6)
#define ICM426XX_SPI_AP_4WIRE BIT(1)

#define ICM426XX_REG_INTF_CONFIG5 0x017B
#define ICM426XX_PIN9_FUNC_INT2 (0x00 << 1)
#define ICM426XX_PIN9_FUNC_FSYNC (0x01 << 1)

#define ICM426XX_REG_INTF_CONFIG6 0x017C
#define ICM426XX_INTF_CONFIG6_MASK GENMASK(4, 0)
#define ICM426XX_I3C_EN BIT(4)
#define ICM426XX_I3C_IBI_BYTE_EN BIT(3)
#define ICM426XX_I3C_IBI_EN BIT(2)
#define ICM426XX_I3C_DDR_EN BIT(1)
#define ICM426XX_I3C_SDR_EN BIT(0)

#define ICM426XX_REG_INT_SOURCE8 0x044F
#define ICM426XX_FSYNC_IBI_EN BIT(5)
#define ICM426XX_PLL_RDY_IBI_EN BIT(4)
#define ICM426XX_UI_DRDY_IBI_EN BIT(3)
#define ICM426XX_FIFO_THS_IBI_EN BIT(2)
#define ICM426XX_FIFO_FULL_IBI_EN BIT(1)
#define ICM426XX_AGC_RDY_IBI_EN BIT(0)

#define ICM426XX_REG_OFFSET_USER0 0x0477
#define ICM426XX_REG_OFFSET_USER1 0x0478
#define ICM426XX_REG_OFFSET_USER2 0x0479
#define ICM426XX_REG_OFFSET_USER3 0x047A
#define ICM426XX_REG_OFFSET_USER4 0x047B
#define ICM426XX_REG_OFFSET_USER5 0x047C
#define ICM426XX_REG_OFFSET_USER6 0x047D
#define ICM426XX_REG_OFFSET_USER7 0x047E
#define ICM426XX_REG_OFFSET_USER8 0x047F

extern const struct accelgyro_drv icm426xx_drv;

void icm426xx_interrupt(enum gpio_signal signal);

#if defined(CONFIG_ZEPHYR)
#if DT_NODE_EXISTS(DT_ALIAS(icm426xx_int))

/* Get the motion sensor ID of the ICM426xx sensor that generates the interrupt.
 * The interrupt is converted to the event and transferred to motion sense task
 * that actually handles the interrupt.
 *
 * Here we use an alias (icm426xx_int) to get the motion sensor ID. This alias
 * MUST be defined for this driver to work.
 * aliases {
 *   icm426xx-int = &base_accel;
 * };
 */
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(icm426xx_int)))
#endif
#endif /* defined(CONFIG_ZEPHYR) */

#endif /* __CROS_EC_ACCELGYRO_ICM426XX_H */
