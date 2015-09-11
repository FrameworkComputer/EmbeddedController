/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMM150 magnetometer definition */

#ifndef __CROS_EC_MAG_BMM150_H
#define __CROS_EC_MAG_BMM150_H

#include "accelgyro.h"
#include "mag_cal.h"

#define BMM150_ADDR0             0x20
#define BMM150_ADDR1             0x22
#define BMM150_ADDR2             0x24
#define BMM150_ADDR3             0x26

#define BMM150_CHIP_ID           0x40
#define BMM150_CHIP_ID_MAJOR     0x32

#define BMM150_BASE_DATA         0x42

#define BMM150_INT_STATUS        0x4a
#define BMM150_PWR_CTRL          0x4b
#define BMM150_SRST                  ((1 << 7) | (1 << 1))
#define BMM150_PWR_ON                (1 << 0)

#define BMM150_OP_CTRL           0x4c
#define BMM150_OP_MODE_OFFSET    1
#define BMM150_OP_MODE_MASK      3
#define BMM150_OP_MODE_NORMAL    0x00
#define BMM150_OP_MODE_FORCED    0x01
#define BMM150_OP_MODE_SLEEP     0x03

#define BMM150_INT_CTRL          0x4d

#define BMM150_REPXY             0x51
#define BMM150_LOW_POWER_nXY         3
#define BMM150_REGULAR_nXY           9
#define BMM150_ENHANCED_nXY         15
#define BMM150_HIGH_ACCURACY_nXY    47
#define BMM150_REPZ              0x52
#define BMM150_LOW_POWER_nZ          3
#define BMM150_REGULAR_nZ           15
#define BMM150_ENHANCED_nZ          27
#define BMM150_HIGH_ACCURACY_nZ     83

#define BMM150_REP(_preset, _axis) CONCAT4(BMM150_, _preset, _n, _axis)

/* Hidden registers for RHALL calculation */
#define BMM150_REGA_DIG_X1       0x5d
#define BMM150_REGA_DIG_Y1       0x5e
#define BMM150_REGA_DIG_Z4_LSB   0x62
#define BMM150_REGA_DIG_Z4_MSB   0x63
#define BMM150_REGA_DIG_X2       0x64
#define BMM150_REGA_DIG_Y2       0x65
#define BMM150_REGA_DIG_Z2_LSB   0x68
#define BMM150_REGA_DIG_Z2_MSB   0x69
#define BMM150_REGA_DIG_Z1_LSB   0x6a
#define BMM150_REGA_DIG_Z1_MSB   0x6b
#define BMM150_REGA_DIG_XYZ1_LSB 0x6c
#define BMM150_REGA_DIG_XYZ1_MSB 0x6d
#define BMM150_REGA_DIG_Z3_LSB   0x6e
#define BMM150_REGA_DIG_Z3_MSB   0x6f
#define BMM150_REGA_DIG_XY2      0x70
#define BMM150_REGA_DIG_XY1      0x71

/* Overflow */

#define BMM150_FLIP_OVERFLOW_ADCVAL             (-4096)
#define BMM150_HALL_OVERFLOW_ADCVAL             (-16384)
#define BMM150_OVERFLOW_OUTPUT                  (0x8000)


struct bmm150_comp_registers {
	/* Local copy of the compensation registers. */
	int8_t       dig1[2];
	int8_t       dig2[2];

	uint16_t     dig_z1;
	int16_t      dig_z2;
	int16_t      dig_z3;
	int16_t      dig_z4;

	uint8_t      dig_xy1;
	int8_t       dig_xy2;

	uint16_t     dig_xyz1;
};

struct bmm150_private_data {
	struct bmm150_comp_registers comp;
	struct mag_cal_t             cal;
};
#define BMM150_COMP_REG(_s) \
	(&BMI160_GET_DATA(_s)->compass.comp)

#define BMM150_CAL(_s) \
	(&BMI160_GET_DATA(_s)->compass.cal)

/* Specific initialization of BMM150 when behing BMI160 */
int bmm150_init(const struct motion_sensor_t *s);

/* Command to normalize and apply temperature compensation */
void bmm150_normalize(const struct motion_sensor_t *s,
		      vector_3_t v,
		      uint8_t *data);

int bmm150_set_offset(const struct motion_sensor_t *s,
		      const vector_3_t offset);

int bmm150_get_offset(const struct motion_sensor_t *s,
		      vector_3_t   offset);

#endif /* __CROS_EC_MAG_BMM150_H */
