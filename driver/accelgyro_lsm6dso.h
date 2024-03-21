/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSO Accel and Gyro driver for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_LSM6DSO_H
#define __CROS_EC_ACCELGYRO_LSM6DSO_H

#include "driver/accelgyro_lsm6dso_public.h"
#include "stm_mems_common.h"

/* Access to embedded sensor hub register bank */
#define LSM6DSO_FUNC_CFG_ACC_ADDR 0x01
#define LSM6DSO_FUNC_CFG_EN 0x80

/* Common defines for Acc and Gyro sensors */
#define LSM6DSO_EN_BIT 0x01
#define LSM6DSO_DIS_BIT 0x00

#define LSM6DSO_GYRO_OUT_X_L_ADDR 0x22
#define LSM6DSO_ACCEL_OUT_X_L_ADDR 0x28

#define LSM6DSO_CTRL1_ADDR 0x10
#define LSM6DSO_CTRL2_ADDR 0x11

#define LSM6DSO_CTRL4_ADDR 0x13
#define LSM6DSO_INT2_ON_INT1_MASK 0x20

#define LSM6DSO_CTRL5_ADDR 0x14
#define LSM6DSO_CTRL6_ADDR 0x15
#define LSM6DSO_CTRL7_ADDR 0x16
#define LSM6DSO_CTRL8_ADDR 0x17
#define LSM6DSO_CTRL9_ADDR 0x18

#define LSM6DSO_CTRL10_ADDR 0x19
#define LSM6DSO_TIMESTAMP_EN 0x20

#define LSM6DSO_STATUS_REG 0x1e

/* Output data rate registers and masks */
#define LSM6DSO_ODR_REG(_sensor) (LSM6DSO_CTRL1_ADDR + (_sensor))
#define LSM6DSO_ODR_MASK 0xf0

/* FIFO decimator registers and bitmask */
#define LSM6DSO_FIFO_CTRL1_ADDR 0x07
#define LSM6DSO_FIFO_CTRL2_ADDR 0x08

#define LSM6DSO_FIFO_CTRL3_ADDR 0x09
#define LSM6DSO_FIFO_ODR_XL_MASK 0x0f
#define LSM6DSO_FIFO_ODR_G_MASK 0xf0

#define LSM6DSO_FIFO_CTRL4_ADDR 0x0a
#define LSM6DSO_FIFO_MODE_MASK 0x07

#define LSM6DSO_INT2_CTRL 0x0e
#define LSM6DSO_INT_FIFO_TH 0x08
#define LSM6DSO_INT_FIFO_OVR 0x10
#define LSM6DSO_INT_FIFO_FULL 0x20

#define LSM6DSO_FIFO_STS1_ADDR 0x3a
#define LSM6DSO_FIFO_STS2_ADDR 0x3b
#define LSM6DSO_FIFO_DIFF_MASK 0x07ff
#define LSM6DSO_FIFO_FULL 0x2000
#define LSM6DSO_FIFO_DATA_OVR 0x4000
#define LSM6DSO_FIFO_WATERMARK 0x8000

/* Out FIFO data register */
#define LSM6DSO_FIFO_DATA_ADDR_TAG 0x78

/* Registers value for supported FIFO mode */
#define LSM6DSO_FIFO_MODE_BYPASS_VAL 0x00
#define LSM6DSO_FIFO_MODE_CONTINUOUS_VAL 0x06

/* Define device available in FIFO pattern */
enum lsm6dso_dev_fifo {
	LSM6DSO_FIFO_DEV_INVALID = -1,
	LSM6DSO_FIFO_DEV_GYRO = 0,
	LSM6DSO_FIFO_DEV_ACCEL,
	LSM6DSO_FIFO_DEV_NUM,
};

/* Define FIFO data pattern, tag and len */
#define LSM6DSO_TAG_SIZE 1
#define LSM6DSO_FIFO_SAMPLE_SIZE (OUT_XYZ_SIZE + LSM6DSO_TAG_SIZE)

enum lsm6dso_tag_fifo {
	LSM6DSO_GYRO_TAG = 0x01,
	LSM6DSO_ACC_TAG = 0x02,
};

struct lsm6dso_fstatus {
	uint16_t len;
	uint16_t pattern;
};

/* ODR reg value from selected data rate in mHz */
#define LSM6DSO_ODR_TO_REG(_odr) (__fls(_odr / LSM6DSO_ODR_MIN_VAL) + 1)

#define LSM6DSO_FIFO_ODR_MASK(_s)                                        \
	(_s->type == MOTIONSENSE_TYPE_ACCEL ? LSM6DSO_FIFO_ODR_XL_MASK : \
					      LSM6DSO_FIFO_ODR_G_MASK)

/* Normalized ODR values from selected data rate in mHz */
#define LSM6DSO_REG_TO_ODR(_reg) (LSM6DSO_ODR_MIN_VAL << (_reg - 1))

/* Full Scale ranges value and gain for Acc */
#define LSM6DSO_FS_LIST_NUM 4

#define LSM6DSO_ACCEL_FS_ADDR 0x10
#define LSM6DSO_ACCEL_FS_MASK 0x0c

#define LSM6DSO_ACCEL_FS_2G_VAL 0x00
#define LSM6DSO_ACCEL_FS_4G_VAL 0x02
#define LSM6DSO_ACCEL_FS_8G_VAL 0x03
#define LSM6DSO_ACCEL_FS_16G_VAL 0x01

#define LSM6DSO_ACCEL_FS_MAX_VAL 16

/* Accel reg value from Full Scale range */
static inline uint8_t lsm6dso_accel_fs_reg(int fs)
{
	uint8_t ret;

	switch (fs) {
	case 2:
		ret = LSM6DSO_ACCEL_FS_2G_VAL;
		break;
	case 16:
		ret = LSM6DSO_ACCEL_FS_16G_VAL;
		break;
	default:
		ret = __fls(fs);
		break;
	}

	return ret;
}

/* Accel normalized FS value from Full Scale */
#define LSM6DSO_ACCEL_NORMALIZE_FS(_fs) (1 << __fls(_fs))

/* Full Scale range value and gain for Gyro */
#define LSM6DSO_GYRO_FS_ADDR 0x11
#define LSM6DSO_GYRO_FS_MASK 0x0c

/* Minimal Gyro range in mDPS */
#define LSM6DSO_GYRO_FS_MIN_VAL_MDPS ((8750 << 15) / 1000)
#define LSM6DSO_GYRO_FS_MAX_REG_VAL 3

/* Gyro reg value for Full Scale selection in DPS */
#define LSM6DSO_GYRO_FS_REG(_fs) \
	__fls(MAX(1, (_fs * 1000) / LSM6DSO_GYRO_FS_MIN_VAL_MDPS))

/* Gyro normalized FS value (in DPS) from Full Scale register */
#define LSM6DSO_GYRO_NORMALIZE_FS(_reg) \
	((LSM6DSO_GYRO_FS_MIN_VAL_MDPS << (_reg)) / 1000)

/* FS register address/mask for Acc/Gyro sensors */
#define LSM6DSO_RANGE_REG(_sensor) (LSM6DSO_ACCEL_FS_ADDR + (_sensor))
#define LSM6DSO_RANGE_MASK 0x0c

/* Status register bit for Acc/Gyro data ready */
enum lsm6dso_status {
	LSM6DSO_STS_DOWN = 0x00,
	LSM6DSO_STS_XLDA_UP = 0x01,
	LSM6DSO_STS_GDA_UP = 0x02
};

/* Status register bitmask for Acc/Gyro data ready */
#define LSM6DSO_STS_XLDA_MASK 0x01
#define LSM6DSO_STS_GDA_MASK 0x02

/* Sensor resolution in number of bits: fixed 16 bit */
#define LSM6DSO_RESOLUTION 16

/* Aggregate private data for all supported sensor (Acc, Gyro) */
struct lsm6dso_data {
	struct stprivate_data st_data[LSM6DSO_FIFO_DEV_NUM];
};

/*
 * Note: The specific number of samples to discard depends on the filters
 * configured for the chip, as well as the ODR being set. For most of our
 * allowed ODRs, 3 should suffice.
 * See: ST's LSM6DSO application notes (AN5192) Tables 12 and 18 for details
 */
#define LSM6DSO_DISCARD_SAMPLES 3

/* Macro to initialize motion_sensors structure */
#define LSM6DSO_ST_DATA(g, type) (&((g).st_data[type]))

extern const struct accelgyro_drv lsm6dso_drv;

void lsm6dso_interrupt(enum gpio_signal signal);

#if defined(CONFIG_ZEPHYR)
#if DT_NODE_EXISTS(DT_ALIAS(lsm6dso_int))
/* Get the motion sensor ID of the LSM6DSO sensor that generates the
 * interrupt. The interrupt is converted to the event and transferred to
 * motion sense task that actually handles the interrupt.
 *
 * Here we use an alias (lsm6dso_int) to get the motion sensor ID. This alias
 * MUST be defined for this driver to work.
 * aliases {
 *   lsm6dso-int = &lid_accel;
 * };
 */
#define CONFIG_ACCEL_LSM6DSO_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(lsm6dso_int)))
#endif
#endif /* CONFIG_ZEPHYR */

#endif /* __CROS_EC_ACCELGYRO_LSM6DSO_H */
