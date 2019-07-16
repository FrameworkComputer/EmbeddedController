/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Rohm BH1730 Ambient light sensor driver
 */

#ifndef __CROS_EC_ALS_BH1730_H
#define __CROS_EC_ALS_BH1730_H

/* I2C interface */
#define BH1730_I2C_ADDR_FLAGS	0x29

/* BH1730 registers */
#define BH1730_CONTROL		0x80
#define BH1730_TIMING		0x81
#define BH1730_INTERRUPT	0x82
#define BH1730_THLLOW		0x83
#define BH1730_THLHIGH		0x84
#define BH1730_THHLOW		0x85
#define BH1730_THHHIGH		0x86
#define BH1730_GAIN		0x87
#define BH1730_OPART_ID		0x92
#define BH1730_DATA0LOW		0x94
#define BH1730_DATA0HIGH	0x95
#define BH1730_DATA1LOW		0x96
#define BH1730_DATA1HIGH	0x97
/* Software reset */
#define BH1730_RESET		0xE4

/* Registers bits */
#define BH1730_CONTROL_ADC_INTR_INACTIVE	(0x00 << 5)
#define BH1730_CONTROL_ADC_INTR_ACTIVE		(0x01 << 5)
#define BH1730_CONTROL_ADC_VALID		(0x01 << 4)
#define BH1730_CONTROL_ONE_TIME_CONTINOUS	(0x00 << 3)
#define BH1730_CONTROL_ONE_TIME_ONETIME		(0x01 << 3)
#define BH1730_CONTROL_DATA_SEL_TYPE0_AND_1	(0x00 << 2)
#define BH1730_CONTROL_DATA_SEL_TYPE0		(0x01 << 2)
#define BH1730_CONTROL_ADC_EN_DISABLE		(0x00 << 1)
#define BH1730_CONTROL_ADC_EN_ENABLE		(0x01 << 1)
#define BH1730_CONTROL_POWER_DISABLE		(0x00 << 0)
#define BH1730_CONTROL_POWER_ENABLE		(0x01 << 0)

#define BH1730_GAIN_GAIN_X1_GAIN		(0x00 << 0)
#define BH1730_GAIN_GAIN_X2_GAIN		(0x01 << 0)
#define BH1730_GAIN_GAIN_X64_GAIN		(0x02 << 0)
#define BH1730_GAIN_GAIN_X128_GAIN		(0x03 << 0)

/* Sensor configuration */
/* Select Gain */
#define BH1730_CONF_GAIN BH1730_GAIN_GAIN_X64_GAIN
#define BH1730_GAIN_DIV 64

/* Select Itime, 0xDA is 102.6ms = 38*2.7ms */
#define BH1730_CONF_ITIME 0xDA
#define ITIME_MS_X_10 ((256 - BH1730_CONF_ITIME) * 27)
#define ITIME_MS_X_1K (ITIME_MS_X_10*100)

/* default Itime is about 10Hz */
#define BH1730_10000_MHZ (10*1000)

/*
 * Use default lux calculation formula parameters if board specific
 * parameters are not defined.
 */
#ifndef CONFIG_ALS_BH1730_LUXTH_PARAMS
#define BH1730_LUXTH1_1K                260
#define BH1730_LUXTH1_D0_1K             1290
#define BH1730_LUXTH1_D1_1K             2733
#define BH1730_LUXTH2_1K                550
#define BH1730_LUXTH2_D0_1K             797
#define BH1730_LUXTH2_D1_1K             859
#define BH1730_LUXTH3_1K                1090
#define BH1730_LUXTH3_D0_1K             510
#define BH1730_LUXTH3_D1_1K             345
#define BH1730_LUXTH4_1K                2130
#define BH1730_LUXTH4_D0_1K             276
#define BH1730_LUXTH4_D1_1K             130
#endif

#define BH1730_GET_DATA(_s)    ((struct bh1730_drv_data_t *)(_s)->drv_data)

struct bh1730_drv_data_t {
	int rate;
	int last_value;
};

extern const struct accelgyro_drv bh1730_drv;

#endif	/* __CROS_EC_ALS_BH1730_H */

