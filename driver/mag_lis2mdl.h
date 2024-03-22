/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LIS2MSL magnetometer module for Chrome EC */

#ifndef __CROS_EC_MAG_LIS2MDL_H
#define __CROS_EC_MAG_LIS2MDL_H

#include "accelgyro.h"
#include "mag_cal.h"
#include "stm_mems_common.h"

/*
 * 8-bit address is 0011110Wb where the last bit represents whether the
 * operation is a read or a write.
 */
#define LIS2MDL_ADDR_FLAGS 0x1e

#define LIS2MDL_STARTUP_MS 10

/* Registers */
#define LIS2MDL_WHO_AM_I_REG 0x4f
#define LIS2MDL_CFG_REG_A_ADDR 0x60
#define LIS2MDL_INT_CTRL_REG 0x63
#define LIS2MDL_STATUS_REG 0x67
#define LIS2MDL_OUT_REG 0x68

#define LIS2MDL_WHO_AM_I 0x40

#define LIS2MDL_FLAG_TEMP_COMPENSATION 0x80
#define LIS2MDL_FLAG_REBOOT 0x40
#define LIS2MDL_FLAG_SW_RESET 0x20
#define LIS2MDL_FLAG_LOW_POWER 0x10
#define LIS2MDL_ODR_50HZ 0x08
#define LIS2MDL_ODR_20HZ 0x04
#define LIS2MDL_ODR_10HZ 0x00
#define LIS2MDL_MODE_IDLE 0x03
#define LIS2MDL_MODE_SINGLE 0x01
#define LIS2MDL_MODE_CONT 0x00
#define LIS2MDL_ODR_MODE_MASK 0x8f

#define LIS2MDL_X_DIRTY 0x01
#define LIS2MDL_Y_DIRTY 0x02
#define LIS2MDL_Z_DIRTY 0x04
#define LIS2MDL_XYZ_DIRTY 0x08
#define LIS2MDL_XYZ_DIRTY_MASK 0x0f

#define LIS2DSL_RESOLUTION 16
/*
 * Maximum sensor data range (milligauss):
 * Spec is 1.5 mguass / LSB, so 0.15 uT / LSB.
 * Calibration code is set to 16LSB/ut, [0.0625 uT/LSB]
 * Apply a multiplier to change the unit
 */
#define LIS2MDL_RATIO(_in) (((_in) * 24) / 10)

struct lis2mdl_private_data {
	/* lsm6dsm_data union requires cal be first element */
	struct mag_cal_t cal;
#ifndef CONFIG_LSM6DSM_SEC_I2C
	struct stprivate_data data;
#endif
#ifdef CONFIG_MAG_BMI_LIS2MDL
	intv3_t hn; /* last sample for offset compensation */
	int hn_valid;
#endif
};

#ifndef CONFIG_LSM6DSM_SEC_I2C
#define LIS2MDL_ST_DATA(g) (&((g).data))

#define LIS2MDL_CAL(_s) \
	(&(DOWNCAST(s->drv_data, struct lis2mdl_private_data, data)->cal))
#endif

#define LIS2MDL_ODR_MIN_VAL 10000
#define LIS2MDL_ODR_MAX_VAL 50000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= LIS2MDL_ODR_MAX_VAL)
#error "EC too slow for magnetometer"
#endif

void lis2mdl_normalize(const struct motion_sensor_t *s, intv3_t v,
		       uint8_t *data);

extern const struct accelgyro_drv lis2mdl_drv;

#endif /* __CROS_EC_MAG_LIS2MDL_H */
