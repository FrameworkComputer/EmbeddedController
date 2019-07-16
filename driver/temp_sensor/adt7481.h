/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADT7481 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_ADT7481_H
#define __CROS_EC_ADT7481_H

#define ADT7481_I2C_ADDR_FLAGS		0x4B

#define ADT7481_IDX_LOCAL	0
#define ADT7481_IDX_REMOTE1	1
#define ADT7481_IDX_REMOTE2	2

/* Chip-specific registers */
#define ADT7481_LOCAL			0x00
#define ADT7481_REMOTE1			0x01
#define ADT7481_STATUS1_R		0x02
#define ADT7481_CONFIGURATION1_R	0x03
#define ADT7481_CONVERSION_RATE_R	0x04
#define ADT7481_LOCAL_HIGH_LIMIT_R	0x05
#define ADT7481_LOCAL_LOW_LIMIT_R	0x06
#define ADT7481_REMOTE1_HIGH_LIMIT_R	0x07
#define ADT7481_REMOTE1_LOW_LIMIT_R	0x08
#define ADT7481_CONFIGURATION1_W	0x09
#define ADT7481_CONVERSION_RATE_W	0x0a
#define ADT7481_LOCAL_HIGH_LIMIT_W	0x0b
#define ADT7481_LOCAL_LOW_LIMIT_W	0x0c
#define ADT7481_REMOTE1_HIGH_LIMIT_W	0x0d
#define ADT7481_REMOTE1_LOW_LIMIT_W	0x0e
#define ADT7481_ONESHOT_W		0x0f
#define ADT7481_REMOTE1_EXTD_R		0x10
#define ADT7481_REMOTE1_OFFSET		0x11
#define ADT7481_REMOTE1_OFFSET_EXTD	0x12
#define ADT7481_REMOTE1_HIGH_LIMIT_EXTD	0x13
#define ADT7481_REMOTE1_LOW_LIMIT_EXTD	0x14
#define ADT7481_REMOTE1_THERM_LIMIT	0x19
#define ADT7481_LOCAL_THERM_LIMIT	0x20
#define ADT7481_THERM_HYSTERESIS	0x21
#define ADT7481_CONSECUTIVE_ALERT	0x22
#define ADT7481_STATUS2_R		0x23
#define ADT7481_CONFIGURATION2		0x24
#define ADT7481_REMOTE2			0x30
#define ADT7481_REMOTE2_HIGH_LIMIT	0x31
#define ADT7481_REMOTE2_LOW_LIMIT	0x32
#define ADT7481_REMOTE2_EXTD_R		0x33
#define ADT7481_REMOTE2_OFFSET		0x34
#define ADT7481_REMOTE2_OFFSET_EXTD	0x35
#define ADT7481_REMOTE2_HIGH_LIMIT_EXTD	0x36
#define ADT7481_REMOTE2_LOW_LIMIT_EXTD	0x37
#define ADT7481_REMOTE2_THERM_LIMIT	0x39
#define ADT7481_DEVICE_ID		0x3d
#define ADT7481_MANUFACTURER_ID		0x3e

/* Config1 register bits */
#define ADT7481_CONFIG1_REMOTE1_ALERT_MASK	BIT(0)
#define ADT7481_CONFIG1_REMOTE2_ALERT_MASK	BIT(1)
#define ADT7481_CONFIG1_TEMP_RANGE		BIT(2)
#define ADT7481_CONFIG1_SEL_REMOTE2		BIT(3)
/* ADT7481_CONFIG1_MODE bit is use to enable THERM mode */
#define ADT7481_CONFIG1_MODE			BIT(5)
#define ADT7481_CONFIG1_RUN_L			BIT(6)
/* mask all alerts on ALERT# pin */
#define ADT7481_CONFIG1_ALERT_MASK_L		BIT(7)

/* Config2 register bits */
#define ADT7481_CONFIG2_LOCK			BIT(7)

/* Conversion Rate/Channel Select Register */
#define ADT7481_CONV_RATE_MASK		(0x0f)
#define ADT7481_CONV_RATE_16S		(0x00)
#define ADT7481_CONV_RATE_8S		(0x01)
#define ADT7481_CONV_RATE_4S		(0x02)
#define ADT7481_CONV_RATE_2S		(0x03)
#define ADT7481_CONV_RATE_1S		(0x04)
#define ADT7481_CONV_RATE_500MS		(0x05)
#define ADT7481_CONV_RATE_250MS		(0x06)
#define ADT7481_CONV_RATE_125MS		(0x07)
#define ADT7481_CONV_RATE_62500US	(0x08)
#define ADT7481_CONV_RATE_31250US	(0x09)
#define ADT7481_CONV_RATE_15500US	(0x0a)
/* continuous mode 73 ms averaging */
#define ADT7481_CONV_RATE_73MS_AVE	(0x0b)
#define ADT7481_CONV_CHAN_SELECT_MASK	(0x30)
#define ADT7481_CONV_CHAN_SEL_ROUND_ROBIN	(0 << 4)
#define ADT7481_CONV_CHAN_SEL_LOCAL		BIT(4)
#define ADT7481_CONV_CHAN_SEL_REMOTE1		(2 << 4)
#define ADT7481_CONV_CHAN_SEL_REMOTE2		(3 << 4)
#define ADT7481_CONV_AVERAGING_L	BIT(7)


/* Status1 register bits */
#define ADT7481_STATUS1_LOCAL_THERM_ALARM	BIT(0)
#define ADT7481_STATUS1_REMOTE1_THERM_ALARM	BIT(1)
#define ADT7481_STATUS1_REMOTE1_OPEN		BIT(2)
#define ADT7481_STATUS1_REMOTE1_LOW_ALARM	BIT(3)
#define ADT7481_STATUS1_REMOTE1_HIGH_ALARM	BIT(4)
#define ADT7481_STATUS1_LOCAL_LOW_ALARM		BIT(5)
#define ADT7481_STATUS1_LOCAL_HIGH_ALARM	BIT(6)
#define ADT7481_STATUS1_BUSY			BIT(7)

/* Status2 register bits */
#define ADT7481_STATUS2_ALERT			BIT(0)
#define ADT7481_STATUS2_REMOTE2_THERM_ALARM	BIT(1)
#define ADT7481_STATUS2_REMOTE2_OPEN		BIT(2)
#define ADT7481_STATUS2_REMOTE2_LOW_ALARM	BIT(3)
#define ADT7481_STATUS2_REMOTE2_HIGH_ALARM	BIT(4)

/* Consecutive Alert register */
#define ADT7481_CONSEC_MASK		(0xf)
#define ADT7481_CONSEC_1		(0x0)
#define ADT7481_CONSEC_2		(0x2)
#define ADT7481_CONSEC_3		(0x6)
#define ADT7481_CONSEC_4		(0xe)
#define ADT7481_CONSEC_EN_SCL_TIMEOUT	BIT(5)
#define ADT7481_CONSEC_EN_SDA_TIMEOUT	BIT(6)
#define ADT7481_CONSEC_MASK_LOCAL_ALERT	BIT(7)


/* Limits */
#define ADT7481_HYSTERESIS_HIGH_LIMIT	255
#define ADT7481_HYSTERESIS_LOW_LIMIT	0

enum adt7481_power_state {
	ADT7481_POWER_OFF = 0,
	ADT7481_POWER_ON,
	ADT7481_POWER_COUNT
};

enum adt7481_channel_id {
	ADT7481_CHANNEL_LOCAL,
	ADT7481_CHANNEL_REMOTE1,
	ADT7481_CHANNEL_REMOTE2,

	ADT7481_CHANNEL_COUNT
};

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read. Idx indicates whether to read die
 *			temperature or external temperature.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int adt7481_get_val(int idx, int *temp_ptr);

/**
 * Power control function of ADT7481 temperature sensor.
 *
 * @param power_on	ADT7481_POWER_ON: turn ADT7481 sensor on.
 *			ADT7481_POWER_OFF: shut ADT7481 sensor down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int adt7481_set_power(enum adt7481_power_state power_on);

/*
 * Set ADT7481 ALERT#/THERM2# pin to THERM mode, and give a limit
 * for a specific channel.
 *
 * @param channel	specific a channel
 *
 * @param limit_c	High limit temperature, default: 85C
 *
 * @param hysteresis	Hysteresis temperature, default: 10C
 *			All channels share the same hysteresis
 *
 * In THERM mode, ALERT# pin will trigger(Low) by itself when any
 * channel's temperature is greater( >= )than channel's limit_c,
 * and release(High) by itself when channel's temperature is lower
 * than (limit_c - hysteresis)
 */
int adt7481_set_therm_limit(int channel, int limit_c, int hysteresis);
#endif /* __CROS_EC_ADT7481_H */
