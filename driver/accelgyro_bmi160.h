/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMI160 accelerometer and gyro and BMM150 compass module for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_BMI160_H
#define __CROS_EC_ACCELGYRO_BMI160_H

#include "accelgyro.h"
#include "mag_bmm150.h"

/*
 * The addr field of motion_sensor support both SPI and I2C:
 *
 * +-------------------------------+---+
 * |    7 bit i2c address          | 0 |
 * +-------------------------------+---+
 * Or
 * +-------------------------------+---+
 * |    SPI device ID              | 1 |
 * +-------------------------------+---+
 */
#define BMI160_SET_SPI_ADDRESS(_addr) (((_addr) << 1) | 1)
#define BMI160_IS_SPI(_addr)        ((_addr) & 1)
#define BMI160_SPI_ADDRESS(_addr)   ((_addr) >> 1)
#define BMI160_I2C_ADDRESS(_addr)   (_addr)

/* I2C addresses */
#define BMI160_ADDR0             0xd0
#define BMI160_ADDR1             0xd2

#define BMI160_CHIP_ID           0x00
#define BMI160_CHIP_ID_MAJOR     0xd1

#define BMI160_SPEC_ACC_STARTUP_TIME_MS     10
#define BMI160_SPEC_GYR_STARTUP_TIME_MS     80
#define BMI160_SPEC_MAG_STARTUP_TIME_MS     60


#define BMI160_ERR_REG         0x02
#define BMI160_PMU_STATUS      0x03
#define BMI160_PMU_MAG_OFFSET               0
#define BMI160_PMU_GYR_OFFSET               2
#define BMI160_PMU_ACC_OFFSET               4
#define BMI160_PMU_SENSOR_STATUS(_sensor_type, _val) \
	(((_val) >> (4 - 2 * (_sensor_type))) & 0x3)
#define BMI160_PMU_SUSPEND                  0
#define BMI160_PMU_NORMAL                   1
#define BMI160_PMU_LOW_POWER                2
#define BMI160_PMU_FAST_STARTUP             3

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
#define BMI160_POR_DETECTED        (1 << 0)
#define BMI160_GYR_SLF_TST         (1 << 1)
#define BMI160_MAG_MAN_OP          (1 << 2)
#define BMI160_FOC_RDY             (1 << 3)
#define BMI160_NVM_RDY             (1 << 4)
#define BMI160_DRDY_MAG            (1 << 5)
#define BMI160_DRDY_GYR            (1 << 6)
#define BMI160_DRDY_ACC            (1 << 7)
#define BMI160_DRDY_OFF(_sensor)   (7 - (_sensor))
#define BMI160_DRDY_MASK(_sensor)  (1 << BMI160_DRDY_OFF(_sensor))

/* first 2 bytes are the interrupt reasons, next 2 some qualifier */
#define BMI160_INT_STATUS_0    0x1c
#define BMI160_STEP_INT            (1 << 0)
#define BMI160_SIGMOT_INT          (1 << 1)
#define BMI160_ANYM_INT            (1 << 2)
#define BMI160_PMU_TRIGGER_INT     (1 << 3)
#define BMI160_D_TAP_INT           (1 << 4)
#define BMI160_S_TAP_INT           (1 << 5)
#define BMI160_ORIENT_INT          (1 << 6)
#define BMI160_FLAT_INT            (1 << 7)

#define BMI160_INT_STATUS_1    0x1d
#define BMI160_HIGHG_INT           (1 << (2 + 8))
#define BMI160_LOWG_INT            (1 << (3 + 8))
#define BMI160_DRDY_INT            (1 << (4 + 8))
#define BMI160_FFULL_INT           (1 << (5 + 8))
#define BMI160_FWM_INT             (1 << (6 + 8))
#define BMI160_NOMO_INT            (1 << (7 + 8))

#define BMI160_INT_MASK            0xFFFF

#define BMI160_INT_STATUS_2    0x1e
#define BMI160_INT_STATUS_3    0x1f
#define BMI160_FIRST_X             (1 << (0 + 16))
#define BMI160_FIRST_Y             (1 << (1 + 16))
#define BMI160_FIRST_Z             (1 << (2 + 16))
#define BMI160_SIGN                (1 << (3 + 16))
#define BMI160_ANYM_OFFSET         0
#define BMI160_TAP_OFFSET          4
#define BMI160_HIGH_OFFSET         8
#define BMI160_INT_INFO(_type, _data) \
(CONCAT2(BMI160_, _data) << CONCAT3(BMI160_, _type, _OFFSET))

#define BMI160_ORIENT_Z            (1 << (6 + 24))
#define BMI160_FLAT                (1 << (7 + 24))

#define BMI160_TEMPERATURE_0   0x20
#define BMI160_TEMPERATURE_1   0x21

#define BMI160_FIFO_LENGTH_0   0x22
#define BMI160_FIFO_LENGTH_1   0x23
#define BMI160_FIFO_LENGTH_MASK    ((1 << 11) - 1)
#define BMI160_FIFO_DATA       0x24
enum fifo_header {
	BMI160_EMPTY = 0x80,
	BMI160_SKIP = 0x40,
	BMI160_TIME = 0x44,
	BMI160_CONFIG = 0x48
};

#define BMI160_FH_MODE_MASK    0xc0
#define BMI160_FH_PARM_OFFSET    2
#define BMI160_FH_PARM_MASK    (0x7 << BMI160_FH_PARM_OFFSET)
#define BMI160_FH_EXT_MASK     0x03


#define BMI160_ACC_CONF        0x40
#define BMI160_ODR_MASK                 0x0F
#define BMI160_ACC_BW_OFFSET            4
#define BMI160_ACC_BW_MASK     (0x7 << BMI160_ACC_BW_OFFSET)

#define BMI160_ACC_RANGE       0x41
#define BMI160_GSEL_2G         0x03
#define BMI160_GSEL_4G         0x05
#define BMI160_GSEL_8G         0x08
#define BMI160_GSEL_16G        0x0c

#define BMI160_GYR_CONF        0x42
#define BMI160_GYR_BW_OFFSET   4
#define BMI160_GYR_BW_MASK     (0x3 << BMI160_GYR_BW_OFFSET)

#define BMI160_GYR_RANGE       0x43
#define BMI160_DPS_SEL_2000    0x00
#define BMI160_DPS_SEL_1000    0x01
#define BMI160_DPS_SEL_500     0x02
#define BMI160_DPS_SEL_250     0x03
#define BMI160_DPS_SEL_125     0x04


#define BMI160_MAG_CONF        0x44

/* odr = 100 / (1 << (8 - reg)) ,within limit */
#define BMI160_ODR_0_78HZ      0x01
#define BMI160_ODR_25HZ	       0x06
#define BMI160_ODR_800HZ       0x0b
#define BMI160_ODR_1600HZ      0x0c
#define BMI160_ODR_3200HZ      0x0d

#define BMI160_REG_TO_ODR(_regval) \
	((_regval) < 8 ? 100000 / (1 << (8 - (_regval))) : \
			 100000 * (1 << ((_regval) - 8)))
#define BMI160_ODR_TO_REG(_odr) \
	((_odr) < 100000 ? (__builtin_clz(100000 / (_odr)) - 23) : \
			   (39 - __builtin_clz((_odr) / 100000)))

#define BMI160_CONF_REG(_sensor)   (0x40 + 2 * (_sensor))
#define BMI160_RANGE_REG(_sensor)  (0x41 + 2 * (_sensor))

#define BMI160_FIFO_DOWNS      0x45
#define BMI160_FIFO_CONFIG_0   0x46
#define BMI160_FIFO_CONFIG_1   0x47
#define BMI160_FIFO_TAG_TIME_EN    (1 << 1)
#define BMI160_FIFO_TAG_INT2_EN    (1 << 2)
#define BMI160_FIFO_TAG_INT1_EN    (1 << 3)
#define BMI160_FIFO_HEADER_EN      (1 << 4)
#define BMI160_FIFO_MAG_EN         (1 << 5)
#define BMI160_FIFO_ACC_EN         (1 << 6)
#define BMI160_FIFO_GYR_EN         (1 << 7)
#define BMI160_FIFO_TARG_INT(_i)  CONCAT3(BMI160_FIFO_TAG_INT, _i, _EN)
#define BMI160_FIFO_SENSOR_EN(_sensor) \
	((_sensor) == MOTIONSENSE_TYPE_ACCEL ? BMI160_FIFO_ACC_EN : \
	  ((_sensor) == MOTIONSENSE_TYPE_GYRO ? BMI160_FIFO_GYR_EN : \
	   BMI160_FIFO_MAG_EN))

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
#define BMI160_INT_ANYMO_X_EN      (1 << 0)
#define BMI160_INT_ANYMO_Y_EN      (1 << 1)
#define BMI160_INT_ANYMO_Z_EN      (1 << 2)
#define BMI160_INT_D_TAP_EN        (1 << 4)
#define BMI160_INT_S_TAP_EN        (1 << 5)
#define BMI160_INT_ORIENT_EN       (1 << 6)
#define BMI160_INT_FLAT_EN         (1 << 7)
#define BMI160_INT_EN_1        0x51
#define BMI160_INT_HIGHG_X_EN      (1 << 0)
#define BMI160_INT_HIGHG_Y_EN      (1 << 1)
#define BMI160_INT_HIGHG_Z_EN      (1 << 2)
#define BMI160_INT_LOW_EN          (1 << 3)
#define BMI160_INT_DRDY_EN         (1 << 4)
#define BMI160_INT_FFUL_EN         (1 << 5)
#define BMI160_INT_FWM_EN          (1 << 6)
#define BMI160_INT_EN_2        0x52
#define BMI160_INT_NOMOX_EN        (1 << 0)
#define BMI160_INT_NOMOY_EN        (1 << 1)
#define BMI160_INT_NOMOZ_EN        (1 << 2)
#define BMI160_INT_STEP_DET_EN     (1 << 3)

#define BMI160_INT_OUT_CTRL    0x53
#define BMI160_INT_EDGE_CTRL       (1 << 0)
#define BMI160_INT_LVL_CTRL        (1 << 1)
#define BMI160_INT_OD              (1 << 2)
#define BMI160_INT_OUTPUT_EN       (1 << 3)
#define BMI160_INT1_CTRL_OFFSET     0
#define BMI160_INT2_CTRL_OFFSET     4
#define BMI160_INT_CTRL(_i, _bit) \
(CONCAT2(BMI160_INT_, _bit) << CONCAT3(BMI160_INT, _i, _CTRL_OFFSET))

#define BMI160_INT_LATCH       0x54
#define BMI160_INT1_INPUT_EN       (1 << 4)
#define BMI160_INT2_INPUT_EN       (1 << 5)
#define BMI160_LATCH_MASK          0xf
#define BMI160_LATCH_NONE          0
#define BMI160_LATCH_FOREVER       0xf

#define BMI160_INT_MAP_0       0x55
#define BMI160_INT_LOWG_STEP       (1 << 0)
#define BMI160_INT_HIGHG           (1 << 1)
#define BMI160_INT_ANYMOTION       (1 << 2)
#define BMI160_INT_NOMOTION        (1 << 3)
#define BMI160_INT_D_TAP           (1 << 4)
#define BMI160_INT_S_TAP           (1 << 5)
#define BMI160_INT_ORIENT          (1 << 6)
#define BMI160_INT_FLAT            (1 << 7)

#define BMI160_INT_MAP_1       0x56
#define BMI160_INT_PMU_TRIG        (1 << 0)
#define BMI160_INT_FFULL           (1 << 1)
#define BMI160_INT_FWM             (1 << 2)
#define BMI160_INT_DRDY            (1 << 3)
#define BMI160_INT1_MAP_OFFSET      4
#define BMI160_INT2_MAP_OFFSET      0
#define BMI160_INT_MAP(_i, _bit) \
(CONCAT2(BMI160_INT_, _bit) << CONCAT3(BMI160_INT, _i, _MAP_OFFSET))
#define BMI160_INT_FIFO_MAP    BMI160_INT_MAP_1

#define BMI160_INT_MAP_2       0x57

#define BMI160_INT_MAP_INT_1   BMI160_INT_MAP_0
#define BMI160_INT_MAP_INT_2   BMI160_INT_MAP_2
#define BMI160_INT_MAP_REG(_i)  CONCAT2(BMI160_INT_MAP_INT_, _i)

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
#define BMI160_FOC_GYRO_EN              (1 << 6)
#define BMI160_FOC_ACC_PLUS_1G          1
#define BMI160_FOC_ACC_MINUS_1G         2
#define BMI160_FOC_ACC_0G               3
#define BMI160_FOC_ACC_Z_OFFSET         0
#define BMI160_FOC_ACC_Y_OFFSET         2
#define BMI160_FOC_ACC_X_OFFSET         4

#define BMI160_CONF            0x6a
#define BMI160_IF_CONF         0x6b
#define BMI160_IF_MODE_OFF     4
#define BMI160_IF_MODE_MASK    3
#define BMI160_IF_MODE_AUTO_OFF 0
#define BMI160_IF_MODE_I2C_IOS  1
#define BMI160_IF_MODE_AUTO_I2C 2

#define BMI160_PMU_TRIGGER     0x6c
#define BMI160_SELF_TEST       0x6d

#define BMI160_OFFSET_ACC70        0x71
#define BMI160_OFFSET_ACC_MULTI_MG      (3900 * 1024)
#define BMI160_OFFSET_ACC_DIV_MG        1000000
#define BMI160_OFFSET_GYR70        0x74
#define BMI160_OFFSET_GYRO_MULTI_MDS    (61 * 1024)
#define BMI160_OFFSET_GYRO_DIV_MDS      1000
#define BMI160_OFFSET_EN_GYR98     0x77
#define BMI160_OFFSET_ACC_EN            (1 << 6)
#define BMI160_OFFSET_GYRO_EN           (1 << 7)


#define BMI160_CMD_REG             0x7e
#define BMI160_CMD_SOFT_RESET      0xb6
#define BMI160_CMD_NOOP            0x00
#define BMI160_CMD_START_FOC       0x03
#define BMI160_CMD_ACC_MODE_OFFSET 0x10
#define BMI160_CMD_ACC_MODE_SUSP   0x10
#define BMI160_CMD_ACC_MODE_NORMAL 0x11
#define BMI160_CMD_ACC_MODE_LOWPOWER 0x12
#define BMI160_CMD_GYR_MODE_SUSP   0x14
#define BMI160_CMD_GYR_MODE_NORMAL 0x15
#define BMI160_CMD_GYR_MODE_FAST_STARTUP 0x17
#define BMI160_CMD_MAG_MODE_SUSP   0x18
#define BMI160_CMD_MAG_MODE_NORMAL 0x19
#define BMI160_CMD_MAG_MODE_LOWPOWER 0x1a
#define BMI160_CMD_MODE_SUSPEND(_sensor_type) \
	(BMI160_CMD_ACC_MODE_OFFSET | (_sensor_type) << 2 | BMI160_PMU_SUSPEND)
#define BMI160_CMD_MODE_NORMAL(_sensor_type) \
	(BMI160_CMD_ACC_MODE_OFFSET | (_sensor_type) << 2 | BMI160_PMU_NORMAL)

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
#define BMI160_FIFO_FLAG_OFFSET        4
#define BMI160_FIFO_ALL_MASK           7

struct bmi160_drv_data_t {
	struct motion_data_t saved_data[3];
	uint8_t              flags;
#ifdef CONFIG_MAG_BMI160_BMM150
	struct bmm150_comp_registers comp_regs;
#endif
};

#define BMI160_GET_DATA(_s) \
	((struct bmi160_drv_data_t *)(_s)->drv_data)
#define BMI160_GET_SAVED_DATA(_s) \
	(&BMI160_GET_DATA(_s)->saved_data[(_s)->type])

extern struct bmi160_drv_data_t g_bmi160_data;

void bmi160_interrupt(enum gpio_signal signal);

#ifdef CONFIG_MAG_BMI160_BMM150
/* Functions to access the compass through the accel/gyro. */
int raw_mag_read8(const int addr, const uint8_t reg, int *data_ptr);
int raw_mag_write8(const int addr, const uint8_t reg, int data);
#endif


#endif /* __CROS_EC_ACCELGYRO_BMI160_H */
