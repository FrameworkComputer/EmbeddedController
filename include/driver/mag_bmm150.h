/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMM150 magnetometer definition */

#ifndef __CROS_EC_MAG_BMM150_H
#define __CROS_EC_MAG_BMM150_H

#include "accelgyro.h"
#include "mag_cal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMM150_ADDR0_FLAGS 0x10
#define BMM150_ADDR1_FLAGS 0x11
#define BMM150_ADDR2_FLAGS 0x12
#define BMM150_ADDR3_FLAGS 0x13

#define BMM150_CHIP_ID 0x40
#define BMM150_CHIP_ID_MAJOR 0x32

#define BMM150_BASE_DATA 0x42

#define BMM150_INT_STATUS 0x4a
#define BMM150_PWR_CTRL 0x4b
#define BMM150_SRST (BIT(7) | BIT(1))
#define BMM150_PWR_ON BIT(0)

#define BMM150_OP_CTRL 0x4c
#define BMM150_OP_MODE_OFFSET 1
#define BMM150_OP_MODE_MASK 3
#define BMM150_OP_MODE_NORMAL 0x00
#define BMM150_OP_MODE_FORCED 0x01
#define BMM150_OP_MODE_SLEEP 0x03

#define BMM150_INT_CTRL 0x4d

#define BMM150_REPXY 0x51
#define BMM150_LOW_POWER_nXY 3
#define BMM150_REGULAR_nXY 9
#define BMM150_ENHANCED_nXY 15
#define BMM150_HIGH_ACCURACY_nXY 47
#define BMM150_SPECIAL_nXY 75
#define BMM150_REPZ 0x52
#define BMM150_LOW_POWER_nZ 3
#define BMM150_REGULAR_nZ 15
#define BMM150_ENHANCED_nZ 27
#define BMM150_HIGH_ACCURACY_nZ 83
#define BMM150_SPECIAL_nZ 27

#define BMM150_REP(_preset, _axis) CONCAT4(BMM150_, _preset, _n, _axis)

/* Hidden registers for RHALL calculation */
#define BMM150_REGA_DIG_X1 0x5d
#define BMM150_REGA_DIG_Y1 0x5e
#define BMM150_REGA_DIG_Z4_LSB 0x62
#define BMM150_REGA_DIG_Z4_MSB 0x63
#define BMM150_REGA_DIG_X2 0x64
#define BMM150_REGA_DIG_Y2 0x65
#define BMM150_REGA_DIG_Z2_LSB 0x68
#define BMM150_REGA_DIG_Z2_MSB 0x69
#define BMM150_REGA_DIG_Z1_LSB 0x6a
#define BMM150_REGA_DIG_Z1_MSB 0x6b
#define BMM150_REGA_DIG_XYZ1_LSB 0x6c
#define BMM150_REGA_DIG_XYZ1_MSB 0x6d
#define BMM150_REGA_DIG_Z3_LSB 0x6e
#define BMM150_REGA_DIG_Z3_MSB 0x6f
#define BMM150_REGA_DIG_XY2 0x70
#define BMM150_REGA_DIG_XY1 0x71

/* Overflow */

#define BMM150_FLIP_OVERFLOW_ADCVAL (-4096)
#define BMM150_HALL_OVERFLOW_ADCVAL (-16384)
#define BMM150_OVERFLOW_OUTPUT (0x8000)

/* Min and Max sampling frequency in mHz */
#define BMM150_MAG_MIN_FREQ 781

/*
 * From Section 4.2.4, max frequency depends on the preset.
 *
 *  Fmax ~= 1 / (145us * nXY + 500us * nZ + 980us)
 *
 *  To be safe, declare only 75% of the value.
 */
#define __BMM150_MAG_MAX_FREQ(_preset) \
	(750000000 /                   \
	 (145 * BMM150_REP(_preset, XY) + 500 * BMM150_REP(_preset, Z) + 980))

#if (__BMM150_MAG_MAX_FREQ(SPECIAL) > CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ)
#error "EC too slow for magnetometer"
#endif

struct bmm150_comp_registers {
	/* Local copy of the compensation registers. */
	int8_t dig1[2];
	int8_t dig2[2];

	uint16_t dig_z1;
	int16_t dig_z2;
	int16_t dig_z3;
	int16_t dig_z4;

	uint8_t dig_xy1;
	int8_t dig_xy2;

	uint16_t dig_xyz1;
};

struct bmm150_private_data {
	/* lsm6dsm_data union requires cal be first element */
	struct mag_cal_t cal;
	struct bmm150_comp_registers comp;
};

#ifdef CONFIG_MAG_BMI_BMM150
#include "accelgyro_bmi_common.h"

#define BMM150_COMP_REG(_s) (&BMI_GET_DATA(_s)->compass.comp)

#define BMM150_CAL(_s) (&BMI_GET_DATA(_s)->compass.cal)
/*
 * Behind a BMI, the BMM150 is in forced mode. Be sure to choose a frequency
 * compatible with BMI.
 */
#define BMM150_MAG_MAX_FREQ(_preset) \
	BMI_REG_TO_ODR(BMI_ODR_TO_REG(__BMM150_MAG_MAX_FREQ(_preset)))
#else
#define BMM150_COMP_REG(_s) NULL
#define BMM150_CAL(_s) NULL
#define BMM150_MAG_MAX_FREQ(_preset) __BMM150_MAG_MAX_FREQ(_preset)
#endif

/* Specific initialization of BMM150 when behing BMI160 */
int bmm150_init(struct motion_sensor_t *s);

/* Command to normalize and apply temperature compensation */
void bmm150_normalize(const struct motion_sensor_t *s, intv3_t v,
		      uint8_t *data);

int bmm150_set_offset(const struct motion_sensor_t *s, const intv3_t offset);

int bmm150_get_offset(const struct motion_sensor_t *s, intv3_t offset);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MAG_BMM150_H */
