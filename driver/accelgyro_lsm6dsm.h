/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LSM6DSM (also LSM6DSL) Accel and Gyro driver for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_LSM6DSM_H
#define __CROS_EC_ACCELGYRO_LSM6DSM_H

#include "stm_mems_common.h"
#include "mag_cal.h"
#include "mag_bmm150.h"
#include "mag_lis2mdl.h"

/*
 * 7-bit address is 110101xb. Where 'x' is determined
 * by the voltage on the ADDR pin
 */
#define LSM6DSM_ADDR0_FLAGS		0x6a
#define LSM6DSM_ADDR1_FLAGS		0x6b

/* COMMON DEFINE FOR ACCEL-GYRO SENSORS */
#define LSM6DSM_EN_BIT			0x01
#define LSM6DSM_DIS_BIT			0x00

/* Access to embedded sensor hub register bank */
#define LSM6DSM_FUNC_CFG_ACC_ADDR	0x01
#define LSM6DSM_FUNC_CFG_EN			0x80
#define LSM6DSM_FUNC_CFG_EN_B			0x20

/* FIFO decimator registers and bitmask */
#define LSM6DSM_FIFO_CTRL1_ADDR		0x06

/* Output data rate registers and masks */
#define LSM6DSM_ODR_REG(_sensor) \
	(LSM6DSM_CTRL1_ADDR + _sensor)
#define LSM6DSM_ODR_MASK		0xf0

#define LSM6DSM_FIFO_CTRL2_ADDR		0x07

#define LSM6DSM_FIFO_CTRL3_ADDR		0x08
#define LSM6DSM_FIFO_DEC_XL_OFF			0
#define LSM6DSM_FIFO_DEC_G_OFF			3

#define LSM6DSM_FIFO_CTRL4_ADDR		0x09

#define LSM6DSM_FIFO_DECIMATOR(_dec) \
	(_dec < 8 ? _dec : (2 + __builtin_ctz(_dec)))

/* Hardware FIFO size in byte */
#define LSM6DSM_MAX_FIFO_SIZE		4096
#define LSM6DSM_MAX_FIFO_LENGTH	(LSM6DSM_MAX_FIFO_SIZE / OUT_XYZ_SIZE)

#define LSM6DSM_FIFO_CTRL5_ADDR		0x0a
#define LSM6DSM_FIFO_CTRL5_ODR_OFF		3
#define LSM6DSM_FIFO_CTRL5_ODR_MASK \
	(0xf << LSM6DSM_FIFO_CTRL5_ODR_OFF)
#define LSM6DSM_FIFO_CTRL5_MODE_MASK		0x07

#define LSM6DSM_INT1_CTRL		0x0d
#define LSM6DSM_INT_FIFO_TH			0x08
#define LSM6DSM_INT_FIFO_OVR			0x10
#define LSM6DSM_INT_FIFO_FULL			0x20
#define LSM6DSM_INT_SIGMO			0x40

/* Who Am I */
#define LSM6DSM_WHO_AM_I_REG		0x0f
/* LSM6DSM/LSM6DSL/LSM6DS3TR-C */
#define LSM6DSM_WHO_AM_I			0x6a
/* LSM6DS3 */
#define LSM6DS3_WHO_AM_I			0x69

#define LSM6DSM_CTRL1_ADDR		0x10
#define LSM6DSM_XL_ODR_MASK			0xf0

#define LSM6DSM_CTRL2_ADDR		0x11
#define LSM6DSM_CTRL3_ADDR		0x12
#define LSM6DSM_SW_RESET			0x01
#define LSM6DSM_IF_INC				0x04
#define LSM6DSM_PP_OD				0x10
#define LSM6DSM_H_L_ACTIVE			0x20
#define LSM6DSM_BDU				0x40
#define LSM6DSM_BOOT				0x80

#define LSM6DSM_CTRL4_ADDR		0x13
#define LSM6DSM_INT2_ON_INT1_MASK		0x20

#define LSM6DSM_CTRL6_ADDR		0x15
#define LSM6DSM_CTRL7_ADDR		0x16

#define LSM6DSM_CTRL10_ADDR		0x19
#define LSM6DSM_FUNC_EN_MASK			0x04
#define LSM6DSM_SIG_MOT_MASK			0x01
#define LSM6DSM_EMBED_FUNC_EN			0x04
#define LSM6DSM_SIG_MOT_EN			0x01

/* Master mode configuration register */
#define LSM6DSM_MASTER_CFG_ADDR		0x1a
#define LSM6DSM_PASSTROUGH_MASK			0x1f
#define LSM6DSM_EXT_TRIGGER_EN			0x10
#define LSM6DSM_PULLUP_EN			0x08
#define LSM6DSM_I2C_PASS_THRU_MODE		0x04
#define LSM6DSM_I2C_MASTER_ON			0x01

#define LSM6DSM_TAP_SRC_ADDR		0x1c
#define LSM6DSM_STAP_DETECT			0x20
#define LSM6DSM_DTAP_DETECT			0x10

#define LSM6DSM_STATUS_REG		0x1e

#define LSM6DSM_OUT_TEMP_L_ADDR		0x20

#define LSM6DSM_GYRO_OUT_X_L_ADDR	0x22
#define LSM6DSM_ACCEL_OUT_X_L_ADDR	0x28

#define LSM6DSM_SENSORHUB1_REG		0x2e

#define LSM6DSM_FIFO_STS1_ADDR		0x3a
#define LSM6DSM_FIFO_STS2_ADDR		0x3b
#define LSM6DSM_FIFO_DIFF_MASK			0x0fff
#define LSM6DSM_FIFO_EMPTY			0x1000
#define LSM6DSM_FIFO_FULL			0x2000
#define LSM6DSM_FIFO_DATA_OVR			0x4000
#define LSM6DSM_FIFO_WATERMARK			0x8000
#define LSM6DSM_FIFO_NODECIM			0x01

/* Out data register */
#define LSM6DSM_FIFO_DATA_ADDR		0x3e

/* Registers value for supported FIFO mode */
#define LSM6DSM_FIFO_MODE_BYPASS_VAL		0x00
#define LSM6DSM_FIFO_MODE_CONTINUOUS_VAL	0x06

#define LSM6DSM_FUNC_SRC1_ADDR		0x53
#define LSM6DSM_SENSORHUB_END_OP		0x01
#define LSM6DSM_SIGN_MOTION_IA			0x40

#define LSM6DSM_LIR_ADDR		0x58
#define LSM6DSM_LIR_MASK			0x01
#define LSM6DSM_EN_INT				0x80
#define LSM6DSM_EN_TAP				0x0e
#define LSM6DSM_TAP_MASK			0x8e

#define LSM6DSM_TAP_THS_6D		0x59
#define LSM6DSM_D4D_EN_MASK			0x80
#define LSM6DSM_TAP_TH_MASK			0x1f

#define LSM6DSM_INT_DUR2_ADDR		0x5a
#define LSM6DSM_TAP_DUR_MASK			0xf0
#define LSM6DSM_TAP_QUIET_MASK			0x0c

#define LSM6DSM_WUP_THS_ADDR		0x5b
#define LSM6DSM_S_D_TAP_MASK			0x80
#define LSM6DSM_STAP_EN				0
#define LSM6DSM_DTAP_EN				1

#define LSM6DSM_MD1_CFG_ADDR		0x5e
#define LSM6DSM_INT1_STAP			0x40
#define LSM6DSM_INT1_DTAP			0x08

/* Register values for Sensor Hub Slave 0 / Bank A */
#define LSM6DSM_SLV0_ADD_ADDR		0x02
#define LSM6DSM_SLV0_ADDR_SHFT			1
#define LSM6DSM_SLV0_ADDR_MASK			0xfe
#define LSM6DSM_SLV0_RD_BIT			0x01

#define LSM6DSM_SLV0_SUBADD_ADDR	0x03

#define LSM6DSM_SLV0_CONFIG_ADDR	0x04
#define LSM6DSM_SLV0_SLV_RATE_SHFT		6
#define LSM6DSM_SLV0_SLV_RATE_MASK		0xc0
#define LSM6DSM_SLV0_AUX_SENS_SHFT		4
#define LSM6DSM_SLV0_AUX_SENS_MASK		0x30
#define LSM6DSM_SLV0_NUM_OPS_MASK		0x07

#define LSM6DSM_SLV1_CONFIG_ADDR	0x07
#define LSM6DSM_SLV0_WR_ONCE_MASK		0x20

#define LSM6DSM_DATA_WRITE_SUB_SLV0_ADDR 0x0e

/* Define device available in FIFO pattern */
enum dev_fifo {
	FIFO_DEV_INVALID = -1,
	FIFO_DEV_GYRO = 0,
	FIFO_DEV_ACCEL,
#ifdef CONFIG_LSM6DSM_SEC_I2C
	FIFO_DEV_MAG,
#endif
	FIFO_DEV_NUM,
};

struct fstatus {
	uint16_t len;
	uint16_t pattern;
};

/* Absolute maximum rate for acc and gyro sensors */
#define LSM6DSM_ODR_MIN_VAL		13000
#define LSM6DSM_ODR_MAX_VAL \
	MOTION_MAX_SENSOR_FREQUENCY(416000, LSM6DSM_ODR_MIN_VAL)

/* ODR reg value from selected data rate in mHz */
#define LSM6DSM_ODR_TO_REG(_odr) (__fls(_odr / LSM6DSM_ODR_MIN_VAL) + 1)

/* normalized ODR value from selected data rate in mHz */
#define LSM6DSM_REG_TO_ODR(_reg) (LSM6DSM_ODR_MIN_VAL << (_reg - 1))

/* Full Scale range value and gain for Acc */
#define LSM6DSM_FS_LIST_NUM		4

#define LSM6DSM_ACCEL_FS_ADDR		0x10
#define LSM6DSM_ACCEL_FS_MASK		0x0c

#define LSM6DSM_ACCEL_FS_2G_VAL		0x00
#define LSM6DSM_ACCEL_FS_4G_VAL		0x02
#define LSM6DSM_ACCEL_FS_8G_VAL		0x03
#define LSM6DSM_ACCEL_FS_16G_VAL	0x01

#define LSM6DSM_ACCEL_FS_MAX_VAL	16

/* Accel Reg value from Full Scale */
#define LSM6DSM_ACCEL_FS_REG(_fs) \
	(_fs == 2 ? LSM6DSM_ACCEL_FS_2G_VAL : \
	_fs == 16 ? LSM6DSM_ACCEL_FS_16G_VAL : \
	__fls(_fs))

/* Accel normalized FS value from Full Scale */
#define LSM6DSM_ACCEL_NORMALIZE_FS(_fs) (1 << __fls(_fs))

/* Full Scale range value and gain for Gyro */
#define LSM6DSM_GYRO_FS_ADDR		0x11
#define LSM6DSM_GYRO_FS_MASK		0x0c


/* Supported gyroscope ranges:
 * name(dps) | register | gain(udps/LSB) | actual value(dps)
 * 250       | 0        | 8750           |  286.72
 * 500       | 1        | 17500          |  573.44
 * 1000      | 2        | 35000          |  1146.88
 * 2000      | 3        | 70000          |  2293.76
 */
#define LSM6DSM_GYRO_FS_MIN_VAL_MDPS ((8750 << 15) / 1000)
#define LSM6DSM_GYRO_FS_MAX_REG_VAL 3

/* Gyro Reg value for Full Scale selection */
#define LSM6DSM_GYRO_FS_REG(_fs) \
	__fls(MAX(1, (_fs * 1000) / LSM6DSM_GYRO_FS_MIN_VAL_MDPS))

/* Gyro normalized FS value from Full Scale register */
#define LSM6DSM_GYRO_NORMALIZE_FS(_reg) \
	((LSM6DSM_GYRO_FS_MIN_VAL_MDPS << (_reg)) / 1000)

/* FS register address/mask for Acc/Gyro sensors */
#define LSM6DSM_RANGE_REG(_sensor)  (LSM6DSM_ACCEL_FS_ADDR + (_sensor))
#define LSM6DSM_RANGE_MASK		0x0c

/* Status register bitmask for Acc/Gyro data ready */
enum lsm6dsm_status {
	LSM6DSM_STS_DOWN = 0x00,
	LSM6DSM_STS_XLDA_UP = 0x01,
	LSM6DSM_STS_GDA_UP = 0x02
};

#define LSM6DSM_STS_XLDA_MASK		0x01
#define LSM6DSM_STS_GDA_MASK		0x02

/* Sensor resolution in number of bits: fixed 16 bit */
#define LSM6DSM_RESOLUTION		16

extern const struct accelgyro_drv lsm6dsm_drv;

void lsm6dsm_interrupt(enum gpio_signal signal);

struct lsm6dsm_fifo_data {
	/*
	 * FIFO data order is based on the ODR of each sensors.
	 * For example Acc @ 52 Hz, Gyro @ 26 Hz Mag @ 13 Hz in FIFO we have
	 * for each pattern this data samples:
	 *  ________ _______ _______ _______ ________ _______ _______
	 * | Gyro_0 | Acc_0 | Mag_0 | Acc_1 | Gyro_1 | Acc_2 | Acc_3 |
	 * |________|_______|_______|_______|________|_______|_______|
	 *
	 * Total samples for each pattern: 2 Gyro, 4 Acc, 1 Mag
	 */
	/* Calculated samples in a pattern, based on ODR. */
	int samples_in_pattern[FIFO_DEV_NUM];

	/* Sum of all samples_in_pattern. */
	int total_samples_in_pattern;
};

/*
 * Structure used to maintain the load state per sensor. This will be used to
 * properly spread values in case we have more than one reading for a given
 * sensor in a single fifo read pass.
 */
struct load_fifo_sensor_state_t {
	uint32_t int_timestamp;
	uint8_t sample_count;
	int sample_rate;
};

/**
 * Structure used to hold fifo state. This struct should only be used if
 * CONFIG_ACCEL_FIFO is defined.
 */
struct lsm6dsm_accel_fifo_state {
	struct lsm6dsm_fifo_data config;
	struct lsm6dsm_fifo_data current;
	int next_in_pattern;
	/*
	 * After an ODR change, the sensor filters need settling time; discard
	 * initial samples with incorrect values
	 */
	unsigned int samples_to_discard[FIFO_DEV_NUM];
	struct load_fifo_sensor_state_t load_fifo_sensor_state[FIFO_DEV_NUM];
};

/*
 * lsm6dsm_data is used for accel gyro and the sensor connect to a LSM6DSM.
 *
 * +---- lsm6dsm_data ------------------------------------------------+
 * | +--- stprivate_data ---+                                         |
 * | |                      | ST common data for accelerometer        |
 * | +----------------------+                                         |
 * | +--- stprivate_data ---+                                         |
 * | |                      | ST common data for gyroscope            |
 * | +----------------------+                                         |
 * | +--- stprivate_data ---+                                         |
 * | |                      | ST common data for LIS2MDL magnetomer   |
 * | +----------------------+ (optional)                              |
 * |                                                                  |
 * | Fifo Information                                                 |
 * |                                                                  |
 * | +----- Magnetometer information -----------------------------+   |
 * | | +--- mag_cal_t ------+                                     |   |
 * | | |                    | Data for online calibration         |   |
 * | | +--------------------+                                     |   |
 * | | Other privata data                                         |   |
 * | +------------------------------------------------------------+   |
 * +------------------------------------------------------------------+
 *
 * In motion_sensors array, use LSM6DSM_ST_DATA to point drv_data
 * to the right st_data structure.
 */
struct lsm6dsm_data {
#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
	/* LIS2MDL uses st_mems_common and needs stprivate_data */
	struct stprivate_data st_data[3];
#else
	/* BMM150 doesn't use st_mems_common; no stprivate_data */
	struct stprivate_data st_data[2];
#endif
	struct lsm6dsm_accel_fifo_state *accel_fifo_state;
#if defined(CONFIG_LSM6DSM_SEC_I2C) && defined(CONFIG_MAG_CALIBRATE)
	union {
#ifdef CONFIG_MAG_LSM6DSM_BMM150
		struct bmm150_private_data   compass;
#endif
#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
		struct lis2mdl_private_data  compass;
#endif
		struct mag_cal_t             cal;
	};
#endif  /* CONFIG_MAG_CALIBRATE */
};

#ifdef CONFIG_ACCEL_FIFO
#define LSM6DSM_ACCEL_FIFO_STATE (&((struct lsm6dsm_accel_fifo_state) {}))
#else
#define LSM6DSM_ACCEL_FIFO_STATE NULL
#endif

#define LSM6DSM_DATA \
	((struct lsm6dsm_data) { \
		.accel_fifo_state = LSM6DSM_ACCEL_FIFO_STATE, \
	})

/*
 * Note: The specific number of samples to discard depends on the filters
 * configured for the chip, as well as the ODR being set.  For most of our
 * allowed ODRs, 5 should suffice.
 * See: ST's LSM6DSM application notes (AN4987) Tables 17 and 19 for details
 */
#define LSM6DSM_DISCARD_SAMPLES 5

#define LSM6DSM_ST_DATA(g, type) (&(&(g))->st_data[(type)])

#define LSM6DSM_MAIN_SENSOR(_s) ((_s) - (_s)->type)

#define LSM6DSM_GET_DATA(_s) \
	((struct lsm6dsm_data *)(LSM6DSM_MAIN_SENSOR(_s))->drv_data)

#if defined(CONFIG_LSM6DSM_SEC_I2C) && defined(CONFIG_MAG_CALIBRATE)
#define LIS2MDL_CAL(_s) (&LSM6DSM_GET_DATA(_s)->cal)
#endif

int lsm6dsm_set_data_rate(const struct motion_sensor_t *s, int rate, int rnd);


#endif /* __CROS_EC_ACCELGYRO_LSM6DSM_H */
