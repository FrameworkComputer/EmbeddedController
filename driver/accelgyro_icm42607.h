/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ICM-42607 accelerometer and gyroscope for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_ICM42607_H
#define __CROS_EC_ACCELGYRO_ICM42607_H

#include "accelgyro.h"
#include "common.h"

/*
 * 7-bit address is 110100Xb. Where 'X' is determined
 * by the logic level on pin AP_AD0.
 */
#define ICM42607_ADDR0_FLAGS 0x68
#define ICM42607_ADDR1_FLAGS 0x69

/* Min and Max sampling frequency in mHz */
#define ICM42607_ACCEL_MIN_FREQ 1562
#define ICM42607_ACCEL_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(400000, 100000)
#define ICM42607_GYRO_MIN_FREQ 12500
#define ICM42607_GYRO_MAX_FREQ MOTION_MAX_SENSOR_FREQUENCY(1600000, 100000)

/* Min and Max Accel FS in g */
#define ICM42607_ACCEL_FS_MIN_VAL 2
#define ICM42607_ACCEL_FS_MAX_VAL 16

/* Min and Max Gyro FS in dps */
#define ICM42607_GYRO_FS_MIN_VAL 250
#define ICM42607_GYRO_FS_MAX_VAL 2000

/* accel stabilization time in us */
#define ICM42607_ACCEL_START_TIME 20000
#define ICM42607_ACCEL_STOP_TIME 0

/* gyro stabilization time in us */
#define ICM42607_GYRO_START_TIME 40000
#define ICM42607_GYRO_STOP_TIME 20000

/* Reg value from Accel FS in G */
#define ICM42607_ACCEL_FS_TO_REG(_fs) \
	((_fs) <= 2 ? 3 : (_fs) >= 16 ? 0 : 3 - __fls((_fs) / 2))

/* Accel FSR in G from Reg value */
#define ICM42607_ACCEL_REG_TO_FS(_reg) ((1 << (3 - (_reg))) * 2)

/* Reg value from Gyro FS in dps */
#define ICM42607_GYRO_FS_TO_REG(_fs) \
	((_fs) <= 250 ? 3 : (_fs) >= 2000 ? 0 : 3 - __fls((_fs) / 250))

/* Gyro FSR in dps from Reg value */
#define ICM42607_GYRO_REG_TO_FS(_reg) ((1 << (3 - (_reg))) * 250)

/* Reg value from ODR in mHz */
#define ICM42607_ODR_TO_REG(_odr) \
	((_odr) == 0 ? 0 : (__fls(1600000 / (_odr)) + 5))

/* ODR in mHz from Reg value */
#define ICM42607_REG_TO_ODR(_reg) \
	((_reg) <= 5 ? 1600000 : (1600000 / (1 << ((_reg)-5))))

/* Reg value for the next higher ODR */
#define ICM42607_ODR_REG_UP(_reg) ((_reg)-1)

/*
 * Filter bandwidth values from ODR reg
 *   >= 400Hz (7) -> 180Hz (1)
 *   200Hz (8) -> 73Hz (3)
 *   100Hz (9) -> 53Hz (4)
 *   50Hz (10) -> 25Hz (6)
 *   <= 25Hz (11) -> 16Hz (7)
 */
#define ICM42607_ODR_TO_FILT_BW(_odr) \
	((_odr) <= 7 ? 1 : (_odr) <= 9 ? (_odr)-5 : (_odr) == 10 ? 6 : 7)

/*
 * Register addresses are virtual address on 16 bits.
 * MSB is coding block selection value for MREG registers
 * and LSB real register address.
 * ex: MREG2 (block 0x28) register 03 => 0x2803
 */
#define ICM42607_REG_MCLK_RDY 0x0000
#define ICM42607_MCLK_RDY BIT(3)

#define ICM42607_REG_DEVICE_CONFIG 0x0001
#define ICM42607_SPI_MODE_1_2 BIT(0)
#define ICM42607_SPI_AP_4WIRE BIT(2)

#define ICM42607_REG_SIGNAL_PATH_RESET 0x0002
#define ICM42607_SOFT_RESET_DEV_CONFIG BIT(4)
#define ICM42607_FIFO_FLUSH BIT(2)

#define ICM42607_REG_DRIVE_CONFIG1 0x0003

#define ICM42607_REG_DRIVE_CONFIG2 0x0004

#define ICM42607_REG_DRIVE_CONFIG3 0x0005

/* default int configuration is pulsed mode, open drain, and active low */
#define ICM42607_REG_INT_CONFIG 0x0006
#define ICM42607_INT2_MASK GENMASK(5, 3)
#define ICM42607_INT2_LATCHED BIT(5)
#define ICM42607_INT2_PUSH_PULL BIT(4)
#define ICM42607_INT2_ACTIVE_HIGH BIT(3)
#define ICM42607_INT1_MASK GENMASK(2, 0)
#define ICM42607_INT1_LATCHED BIT(2)
#define ICM42607_INT1_PUSH_PULL BIT(1)
#define ICM42607_INT1_ACTIVE_HIGH BIT(0)

/* data are 16 bits */
#define ICM42607_REG_TEMP_DATA 0x0009

/* X + Y + Z: 3 * 16 bits */
#define ICM42607_REG_ACCEL_DATA_XYZ 0x000B
#define ICM42607_REG_GYRO_DATA_XYZ 0x0011

#define ICM42607_INVALID_DATA -32768

/* data are 16 bits */
#define ICM42607_REG_TMST_FSYNCH 0x0017

#define ICM42607_REG_PWR_MGMT0 0x001F
#define ICM42607_ACCEL_LP_CLK_SEL BIT(7)
#define ICM42607_IDLE BIT(4)
#define ICM42607_GYRO_MODE_MASK GENMASK(3, 2)
#define ICM42607_GYRO_MODE(_m) (((_m) & 0x03) << 2)
#define ICM42607_ACCEL_MODE_MASK GENMASK(1, 0)
#define ICM42607_ACCEL_MODE(_m) ((_m) & 0x03)

enum icm42607_sensor_mode {
	ICM42607_MODE_OFF,
	ICM42607_MODE_STANDBY,
	ICM42607_MODE_LOW_POWER,
	ICM42607_MODE_LOW_NOISE,
};

#define ICM42607_REG_GYRO_CONFIG0 0x0020
#define ICM42607_REG_ACCEL_CONFIG0 0x0021
#define ICM42607_FS_MASK GENMASK(6, 5)
#define ICM42607_FS_SEL(_fs) (((_fs) & 0x03) << 5)
#define ICM42607_ODR_MASK GENMASK(3, 0)
#define ICM42607_ODR(_odr) ((_odr) & 0x0F)

#define ICM42607_REG_TEMP_CONFIG0 0x0022

enum icm42607_ui_avg {
	ICM42607_UI_AVG_2X,
	ICM42607_UI_AVG_4X,
	ICM42607_UI_AVG_8X,
	ICM42607_UI_AVG_16X,
	ICM42607_UI_AVG_32X,
	ICM42607_UI_AVG_64X,
};

enum icm42607_ui_filt_bw {
	ICM42607_UI_FILT_BW_DISABLED,
	ICM42607_UI_FILT_BW_180HZ,
	ICM42607_UI_FILT_BW_121HZ,
	ICM42607_UI_FILT_BW_73HZ,
	ICM42607_UI_FILT_BW_53HZ,
	ICM42607_UI_FILT_BW_34HZ,
	ICM42607_UI_FILT_BW_25HZ,
	ICM42607_UI_FILT_BW_16HZ,
};

#define ICM42607_REG_GYRO_CONFIG1 0x0023
#define ICM42607_REG_ACCEL_CONFIG1 0x0024
#define ICM42607_UI_AVG_MASK GENMASK(6, 4)
#define ICM42607_UI_AVG_SET(_avg) (((_avg) & 0x07) << 4)
#define ICM42607_UI_FILT_BW_MASK GENMASK(2, 0)
#define ICM42607_UI_FILT_BW_SET(_filt) ((_filt) & 0x07)

#define ICM42607_REG_FIFO_CONFIG1 0x0028
#define ICM42607_REG_FIFO_CONFIG2 0x0029
#define ICM42607_REG_FIFO_CONFIG3 0x002A
#define ICM42607_FIFO_STOP_ON_FULL_MODE BIT(1)
#define ICM42607_FIFO_BYPASS BIT(0)
#define ICM42607_FIFO_MODE_STREAM 0x00

/* FIFO watermark value is 16 bits little endian */
#define ICM42607_REG_FIFO_WM 0x0029

#define ICM42607_REG_INT_SOURCE0 0x002B
#define ICM42607_ST_INT1_EN BIT(7)
#define ICM42607_FSYNC_INT1_EN BIT(6)
#define ICM42607_PLL_RDY_INT1_EN BIT(5)
#define ICM42607_RESET_DONE_INT1_EN BIT(4)
#define ICM42607_DRDY_INT1_EN BIT(3)
#define ICM42607_FIFO_THS_INT1_EN BIT(2)
#define ICM42607_FIFO_FULL_INT1_EN BIT(1)
#define ICM42607_UI_AGC_RDY_INT1_EN BIT(0)

#define ICM42607_REG_INTF_CONFIG0 0x0035
#define ICM42607_FIFO_COUNT_FORMAT BIT(6)
#define ICM42607_FIFO_COUNT_ENDIAN BIT(5)
#define ICM42607_SENSOR_DATA_ENDIAN BIT(4)

#define ICM42607_REG_INTF_CONFIG1 0x0036
#define ICM42607_I3C_SDR_EN BIT(3)
#define ICM42607_I3C_DDR_EN BIT(2)
#define ICM42607_CLKSEL_MASK GENMASK(1, 0)
#define ICM42607_CLKSEL_PLL_ENABLE 0x01

#define ICM42607_REG_INT_STATUS_DRDY 0x0039
#define ICM42607_DATA_RDY_INT BIT(0)

#define ICM42607_REG_INT_STATUS 0x003A
#define ICM42607_ST_INT BIT(7)
#define ICM42607_FSYNC_INT BIT(6)
#define ICM42607_PLL_RDY_INT BIT(5)
#define ICM42607_RESET_DONE_INT BIT(4)
#define ICM42607_FIFO_THS_INT BIT(2)
#define ICM42607_FIFO_FULL_INT BIT(1)
#define ICM42607_AGC_RDY_INT BIT(0)

/* FIFO count is 16 bits */
#define ICM42607_REG_FIFO_COUNT 0x003D

#define ICM42607_REG_FIFO_DATA 0x003F

#define ICM42607_REG_APEX_CONFIG0 0x0025
#define ICM42607_DMP_SRAM_RESET_APEX BIT(0)

#define ICM42607_REG_APEX_CONFIG1 0x0026
#define ICM42607_DMP_ODR_50HZ BIT(1)

#define ICM42607_REG_WHO_AM_I 0x0075
#define ICM42607_CHIP_ICM42607P 0x60
#define ICM42607_CHIP_ICM42608P 0x3F

/* MREG read access registers */
#define ICM42607_REG_BLK_SEL_W 0x0079
#define ICM42607_REG_MADDR_W 0x007A
#define ICM42607_REG_M_W 0x007B

/* MREG write access registers */
#define ICM42607_REG_BLK_SEL_R 0x007C
#define ICM42607_REG_MADDR_R 0x007D
#define ICM42607_REG_M_R 0x007E

/* USER BANK MREG1 */
#define ICM42607_MREG_FIFO_CONFIG5 0x0001
#define ICM42607_FIFO_WM_GT_TH BIT(5)
#define ICM42607_FIFO_RESUME_PARTIAL_RD BIT(4)
#define ICM42607_FIFO_HIRES_EN BIT(3)
#define ICM42607_FIFO_TMST_FSYNC_EN BIT(2)
#define ICM42607_FIFO_GYRO_EN BIT(1)
#define ICM42607_FIFO_ACCEL_EN BIT(0)

#define ICM42607_MREG_OTP_CONFIG 0x002B
#define ICM42607_OTP_COPY_MODE_MASK GENMASK(3, 2)
#define ICM42607_OTP_COPY_TRIM (0x01 << 2)
#define ICM42607_OTP_COPY_ST_DATA (0x03 << 2)

#define ICM42607_MREG_INT_SOURCE7 0x0030
#define ICM42607_MREG_INT_SOURCE8 0x0031
#define ICM42607_MREG_INT_SOURCE9 0x0032
#define ICM42607_MREG_INT_SOURCE10 0x0033

#define ICM42607_MREG_APEX_CONFIG2 0x0044
#define ICM42607_MREG_APEX_CONFIG3 0x0045
#define ICM42607_MREG_APEX_CONFIG4 0x0046
#define ICM42607_MREG_APEX_CONFIG5 0x0047
#define ICM42607_MREG_APEX_CONFIG9 0x0048
#define ICM42607_MREG_APEX_CONFIG10 0x0049
#define ICM42607_MREG_APEX_CONFIG11 0x004A
#define ICM42607_MREG_APEX_CONFIG12 0x0067

#define ICM42607_MREG_OFFSET_USER0 0x004E
#define ICM42607_MREG_OFFSET_USER1 0x004F
#define ICM42607_MREG_OFFSET_USER2 0x0050
#define ICM42607_MREG_OFFSET_USER3 0x0051
#define ICM42607_MREG_OFFSET_USER4 0x0052
#define ICM42607_MREG_OFFSET_USER5 0x0053
#define ICM42607_MREG_OFFSET_USER6 0x0054
#define ICM42607_MREG_OFFSET_USER7 0x0055
#define ICM42607_MREG_OFFSET_USER8 0x0056

/* USER BANK MREG2 */
#define ICM42607_MREG_OTP_CTRL7 0x2806
#define ICM42607_OTP_RELOAD BIT(3)
#define ICM42607_OTP_PWR_DOWN BIT(1)

extern const struct accelgyro_drv icm42607_drv;

void icm42607_interrupt(enum gpio_signal signal);

#if defined(CONFIG_ZEPHYR)
#if DT_NODE_EXISTS(DT_ALIAS(icm42607_int))
/*
 * Get the motion sensor ID of the ICM42607 sensor that generates the interrupt.
 * The interrupt is converted to the event and transferred to motion sense task
 * that actually handles the interrupt.
 *
 * Here we use an alias (icm42607_int) to get the motion sensor ID. This alias
 * MUST be defined for this driver to work.
 * aliases {
 *   icm42607-int = &base_accel;
 * };
 */
#define CONFIG_ACCELGYRO_ICM42607_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(icm42607_int)))
#endif
#endif /* defined(CONFIG_ZEPHYR) */

#endif /* __CROS_EC_ACCELGYRO_ICM42607_H */
