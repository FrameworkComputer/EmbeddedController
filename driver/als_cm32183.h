/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * CAPELLA CM32183 light sensor driver
 */

#ifndef __CROS_EC_ALS_CM32183_H
#define __CROS_EC_ALS_CM32183_H

/* I2C interface */
#define CM32183_I2C_ADDR		0x29

/* CM32183 registers */
#define CM32183_REG_CONFIGURE		0x00

#define CM32183_REG_CONFIGURE_CH_EN			0x0000

/* ALS Sensitivity_mode (BIT 12:11) */
#define CM32183_REG_CONFIGURE_ALS_SENSITIVITY_MASK	GENMASK(12, 11)
#define CM32183_REG_CONFIGURE_ALS_SENSITIVITY_SHIFT		11
#define CM32183_REG_CONFIGURE_ALS_SENSITIVITY_1		0
#define CM32183_REG_CONFIGURE_ALS_SENSITIVITY_2		1
#define CM32183_REG_CONFIGURE_ALS_SENSITIVITY_1_DIV_8		2
#define CM32183_REG_CONFIGURE_ALS_SENSITIVITY_1_DIV_4		3

/*
 * Gain mode
 * 0 Gain*1
 * 1 Gain*2  (bit 10)
 */
#define CM32183_REG_CONFIGURE_GAIN  BIT(10)

/*
 * ALS integration time setting which represents how long
 * ALS can update the readout value (BIT 9:6)
 * BIT 9:6     function
 *  0000         100ms
 *  0001         200ms
 *  0010         400ms
 *  0011         800ms
 */
#define CM32183_REG_CONFIGURE_ALS_INTEGRATION_MASK	GENMASK(9, 6)
#define CM32183_REG_CONFIGURE_ALS_INTEGRATION_SHIFT		6
#define CM32183_REG_CONFIGURE_ALS_INTEGRATION_SET100MS	0
#define CM32183_REG_CONFIGURE_ALS_INTEGRATION_SET200MS	1
#define CM32183_REG_CONFIGURE_ALS_INTEGRATION_SET400MS	2
#define CM32183_REG_CONFIGURE_ALS_INTEGRATION_SET800MS	3

/*
 * ALS interrupt persistence setting.The interrupt pin is
 * triggered while sensor reading is out of threshold windows
 * after consecutive number of measurement cycle. (BIT 5:4)
 *  BIT 5:4      measurement cycle
 *    00                 1
 *    01                 2
 *    10                 4
 *    11                 8
 */
#define CM32183_REG_CONFIGURE_MEASUREMENT_MASK	GENMASK(5, 4)
#define CM32183_REG_CONFIGURE_MEASUREMENT_SHIFT		4
#define CM32183_REG_CONFIGURE_MEASUREMENT_CYCLE_1	0
#define CM32183_REG_CONFIGURE_MEASUREMENT_CYCLE_2	1
#define CM32183_REG_CONFIGURE_MEASUREMENT_CYCLE_4	2
#define CM32183_REG_CONFIGURE_MEASUREMENT_CYCLE_8	3

/*
 * channel selection of interrupt (BIT 3)
 * 0   ALS CH interrupt
 * 1   White CH interrupt
 */
#define CM32183_REG_CONFIGURE_CHANNEL_SELECTION  BIT(3)

/*
 * Channel enable (BIT 2)
 *  0   ALS CH enable only
 *  1   ALS & White CH enable
 */
#define CM32183_REG_CONFIGURE_CHANNEL_ENABLE  BIT(2)

/* enable/disable interrupt function (BIT 1) */
#define CM32183_REG_CONFIGURE_INTERRUPT_ENABLE  BIT(1)

/*
 * how to power on and shutdown sensor (BIT 0)
 * 0   power on
 * 1   shutdown
 */
#define CM32183_REG_CONFIGURE_POWER  BIT(0)

#define CM32183_REG_INT_HSB		0x01
#define CM32183_REG_INT_LSB		0x02
#define CM32183_REG_ALS_RESULT		0x04
#define CM32183_REG_WHITE_RESULT		0x05

#define CM32183_REG_TRIGGER      0x06

#define CM32183_REG_TRIGGER_LOW_THRESHOLD     BIT(15)
#define CM32183_REG_TRIGGER_HIGH_THRESHOLD    BIT(16)

int cm32183_read_lux(int *lux, int af);
int cm32183_init(void);

#endif	/* __CROS_EC_ALS_CM32183_H */
