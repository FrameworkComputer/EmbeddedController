/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMI160 accelerometer and gyro and BMM150 compass module for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_BMI160_H
#define __CROS_EC_ACCELGYRO_BMI160_H

#include "accelgyro.h"

#define BMI160_ADDR0             0xd0
#define BMI160_ADDR1             0xd2

#define BMI160_CHIP_ID           0x00
#define BMI160_CHIP_ID_MAJOR     0xd1

#define BMI160_SPEC_ACC_STARTUP_TIME_MS     10
#define BMI160_SPEC_GYR_STARTUP_TIME_MS     80
#define BMI160_SPEC_MAG_STARTUP_TIME_MS     60


#define BMI160_ERR_REG         0x02
#define BMI160_PMU_STATUS      0x03
#define BMI160_MAG_X_L_G       0x04
#define BMI160_MAG_X_H_G       0x05
#define BMI160_MAG_Y_L_G       0x06
#define BMI160_MAG_Y_H_G       0x07
#define BMI160_MAG_Z_L_G       0x08
#define BMI160_MAG_Z_H_G       0x09
#define BMI160_RHALL_L_G       0x0a
#define BMI160_RHALL_H_G       0x0b
#define BMI160_GYR_X_L_G       0x0c
#define BMI160_GYR_X_H_G       0x0d
#define BMI160_GYR_Y_L_G       0x0e
#define BMI160_GYR_Y_H_G       0x0f
#define BMI160_GYR_Z_L_G       0x10
#define BMI160_GYR_Z_H_G       0x11
#define BMI160_ACC_X_L_G       0x12
#define BMI160_ACC_X_H_G       0x13
#define BMI160_ACC_Y_L_G       0x14
#define BMI160_ACC_Y_H_G       0x15
#define BMI160_ACC_Z_L_G       0x16
#define BMI160_ACC_Z_H_G       0x17

#define BMI160_SENSORTIME_0    0x18
#define BMI160_SENSORTIME_1    0x19
#define BMI160_SENSORTIME_2    0x1a

#define BMI160_STATUS          0x1b
#define BMI160_DRDY_ACC        0x80
#define BMI160_DRDY_GYR        0x40
#define BMI160_DRDY_MAG        0x20
#define BMI160_DRDY_OFF(_sensor)   (7 - (_sensor))
#define BMI160_DRDY_MASK(_sensor)  (1 << BMI160_DRDY_OFF(_sensor))
#define BMI160_NVM_RDY         0x10
#define BMI160_FOC_RDY         0x08
#define BMI160_MAG_MAN_OP      0x04
#define BMI160_GYR_SLF_TST     0x02
#define BMI160_POR_DETECTED    0x01


#define BMI160_INT_STATUS_0    0x1c
#define BMI160_INT_STATUS_1    0x1d
#define BMI160_INT_STATUS_2    0x1e
#define BMI160_INT_STATUS_3    0x1f

#define BMI160_TEMPERATURE_0   0x20
#define BMI160_TEMPERATURE_1   0x21

#define BMI160_FIFO_LENGTH_0   0x22
#define BMI160_FIFO_LENGTH_1   0x23
#define BMI160_FIFO_DATA       0x24

#define BMI160_ACC_CONF        0x40
#define BMI160_GSEL_2G         0X03
#define BMI160_GSEL_4G         0X05
#define BMI160_GSEL_8G         0X08
#define BMI160_GSEL_16G        0X0C

#define BMI160_ODR_MASK                 0x0F

#define BMI160_ACC_BW_OFFSET   4
#define BMI160_ACC_BW_MASK     (0x7 << BMI160_ACC_BW_OFFSET)
#define BMI160_ACC_RANGE       0x41

#define BMI160_DPS_SEL_2000    0x00
#define BMI160_DPS_SEL_1000    0x01
#define BMI160_DPS_SEL_500     0x02
#define BMI160_DPS_SEL_250     0x03
#define BMI160_DPS_SEL_125     0x04

#define BMI160_GYR_CONF        0x42

#define BMI160_GYR_BW_OFFSET   4
#define BMI160_GYR_BW_MASK     (0x3 << BMI160_GYR_BW_OFFSET)
#define BMI160_GYR_RANGE       0x43

#define BMI160_MAG_CONF        0x44

/* odr = 100 / (1 << (8 - reg)) ,within limit */
#define BMI160_ODR_0_78HZ      0x01
#define BMI160_ODR_25HZ	       0x06
#define BMI160_ODR_800HZ       0x0b
#define BMI160_ODR_1600HZ      0x0c
#define BMI160_ODR_3200HZ      0x0d

#define BMI160_REG_TO_ODR(_regval) (100000 / (1 << (8 - (_regval))))
#define BMI160_ODR_TO_REG(_odr) (__builtin_clz(100000 / (_odr)) - 23)

#define BMI160_CONF_REG(_sensor)   (0x40 + 2 * (_sensor))
#define BMI160_RANGE_REG(_sensor)  (0x41 + 2 * (_sensor))

#define BMI160_FIFO_DOWNS      0x45
#define BMI160_FIFO_CONFIG_0   0x46
#define BMI160_FIFO_CONFIG_1   0x47

#define BMI160_MAG_IF_0        0x4b
#define BMI160_MAG_I2C_ADDRESS BMI160_MAG_IF_0
#define BMI160_MAG_IF_1        0x4c
#define BMI160_MAG_I2C_CONTROL BMI160_MAG_IF_1
#define BMI160_MAG_READ_BURST_MASK 3
#define BMI160_MAG_READ_BURST_1    0
#define BMI160_MAG_READ_BURST_2    1
#define BMI160_MAG_READ_BURST_6    2
#define BMI160_MAG_READ_BURST_8    3
#define BMI160_MAG_OFFSET_OFF      3
#define BMI160_MAG_OFFSET_MASK     (0xf << BMI160_MAG_OFFSET_OFF)
#define BMI160_MAG_MANUAL_EN       (1 << 7)

#define BMI160_MAG_IF_2        0x4d
#define BMI160_MAG_I2C_READ_ADDR    BMI160_MAG_IF_2
#define BMI160_MAG_IF_3        0x4e
#define BMI160_MAG_I2C_WRITE_ADDR   BMI160_MAG_IF_3
#define BMI160_MAG_IF_4        0x4f
#define BMI160_MAG_I2C_WRITE_DATA   BMI160_MAG_IF_4
#define BMI160_MAG_I2C_READ_DATA    BMI160_MAG_X_L_G

#define BMI160_INT_EN_0        0x50
#define BMI160_INT_EN_1        0x51
#define BMI160_INT_EN_2        0x52

#define BMI160_INT_OUT_CTRL    0x53
#define BMI160_INT_LATCH       0x54

#define BMI160_INT_MAP_0       0x55
#define BMI160_INT_MAP_1       0x56
#define BMI160_INT_MAP_2       0x57

#define BMI160_INT_DATA_0      0x58
#define BMI160_INT_DATA_1      0x59

#define BMI160_INT_MOTION_0    0x5f
#define BMI160_INT_MOTION_1    0x60
#define BMI160_INT_MOTION_2    0x61
#define BMI160_INT_MOTION_3    0x62

#define BMI160_INT_TAP_0       0x63
#define BMI160_INT_TAP_1       0x64

#define BMI160_INT_ORIENT_0    0x65
#define BMI160_INT_ORIENT_1    0x66

#define BMI160_INT_FLAT_0      0x67
#define BMI160_INT_FLAT_1      0x68

#define BMI160_FOC_CONF        0x69
#define BMI160_CONF            0x6a
#define BMI160_IF_CONF         0x6b
#define BMI160_IF_MODE_OFF     4
#define BMI160_IF_MODE_MASK    3
#define BMI160_IF_MODE_AUTO_OFF 0
#define BMI160_IF_MODE_I2C_IOS  1
#define BMI160_IF_MODE_AUTO_I2C 2

#define BMI160_PMU_TRIGGER     0x6c
#define BMI160_SELF_TEST       0x6d

#define BMI160_CMD_REG             0x7e
#define BMI160_CMD_SOFT_RESET      0xb6
#define BMI160_CMD_NOOP            0x00
#define BMI160_CMD_ACC_MODE_SUSP   0x10
#define BMI160_CMD_ACC_MODE_NORMAL 0x11
#define BMI160_CMD_ACC_MODE_LOWPOWER 0x12
#define BMI160_CMD_GYR_MODE_SUSP   0x14
#define BMI160_CMD_GYR_MODE_NORMAL 0x15
#define BMI160_CMD_GYR_MODE_FAST_STARTUP 0x17
#define BMI160_CMD_MAG_MODE_SUSP   0x18
#define BMI160_CMD_MAG_MODE_NORMAL 0x19
#define BMI160_CMD_MAG_MODE_LOWPOWER 0x1a
#define BMI160_CMD_MODE_NORMAL(_sensor) (0x11 + 4 * (_sensor))
#define BMI160_CMD_MODE_SUSPEND(_sensor) (0x10 + 4 * (_sensor))

#define BMI160_CMD_FIFO_FLUSH      0xb0
#define BMI160_CMD_INT_RESET       0xb1
#define BMI160_CMD_SOFT_RESET      0xb6
#define BMI160_CMD_EXT_MODE_EN_B0  0x37
#define BMI160_CMD_EXT_MODE_EN_B1  0x9a
#define BMI160_CMD_EXT_MODE_EN_B2  0xc0

#define BMI160_CMD_EXT_MODE_ADDR   0x7f
#define BMI160_CMD_PAGING_EN           (1 << 7)
#define BMI160_CMD_TARGET_PAGE         (1 << 4)
#define BMI160_COM_C_TRIM_ADDR 0x85
#define BMI160_COM_C_TRIM              (3 << 4)



#define BMI160_CMD_TGT_PAGE    0
#define BMI160_CMD_TGT_PAGE_COM    1
#define BMI160_CMD_TGT_PAGE_ACC    2
#define BMI160_CMD_TGT_PAGE_GYR    3

#define BMI160_FF_FRAME_LEN_TS          4
#define BMI160_FF_DATA_LEN_ACC          6
#define BMI160_FF_DATA_LEN_GYR          6
#define BMI160_FF_DATA_LEN_MAG          8

#if 0
#define BMI160_DPS_SEL_245     (0 << 3)
#define BMI160_DPS_SEL_500     (1 << 3)
#define BMI160_DPS_SEL_1000    (2 << 3)
#define BMI160_DPS_SEL_2000    (3 << 3)
#define BMI160_GSEL_2G         (0 << 3)
#define BMI160_GSEL_4G         (2 << 3)
#define BMI160_GSEL_8G         (3 << 3)

#define BMI160_RANGE_MASK      (3 << 3)

#define BMI160_ODR_PD          (0 << 5)
#define BMI160_ODR_10HZ        (1 << 5)
#define BMI160_ODR_15HZ        (1 << 5)
#define BMI160_ODR_50HZ        (2 << 5)
#define BMI160_ODR_59HZ        (2 << 5)
#define BMI160_ODR_119HZ       (3 << 5)
#define BMI160_ODR_238HZ       (4 << 5)
#define BMI160_ODR_476HZ       (5 << 5)
#define BMI160_ODR_952HZ       (6 << 5)

#define BMI160_ODR_MASK        (7 << 5)

/*
 * Register      : STATUS_REG
 * Address       : 0X27
 */
enum bmi160_status {
	BMI160_STS_DOWN                = 0x00,
	BMI160_STS_XLDA_UP             = 0x01,
	BMI160_STS_GDA_UP              = 0x02,
};
#define         BMI160_STS_XLDA_MASK       0x01
#define         BMI160_STS_GDA_MASK        0x02

/*
 * Register      : CTRL_REG8
 * Address       : 0X22
 * Bit Group Name: BDU
 */
enum bmi160_bdu {
	BMI160_BDU_DISABLE              = 0x00,
	BMI160_BDU_ENABLE               = 0x40,
};
#endif

/* Sensor resolution in number of bits. This sensor has fixed resolution. */
#define BMI160_RESOLUTION      16

extern const struct accelgyro_drv bmi160_drv;

enum bmi160_running_mode {
	STANDARD_UI_9DOF_FIFO          = 0,
	STANDARD_UI_IMU_FIFO           = 1,
	STANDARD_UI_IMU                = 2,
	STANDARD_UI_ADVANCEPOWERSAVE   = 3,
	ACCEL_PEDOMETER                = 4,
	APPLICATION_HEAD_TRACKING      = 5,
	APPLICATION_NAVIGATION         = 6,
	APPLICATION_REMOTE_CONTROL     = 7,
	APPLICATION_INDOOR_NAVIGATION  = 8,
};

#define BMI160_FLAG_SEC_I2C_ENABLED    (1 << 0)
struct bmi160_drv_data_t {
	struct motion_data_t saved_data[3];
	uint8_t       flags;
};
#endif /* __CROS_EC_ACCELGYRO_BMI160_H */
