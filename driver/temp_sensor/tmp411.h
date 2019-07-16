/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP411 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TMP411_H
#define __CROS_EC_TMP411_H

#define TMP411_I2C_ADDR_FLAGS		0x4C

#define TMP411_IDX_LOCAL	0
#define TMP411_IDX_REMOTE1	1
#define TMP411_IDX_REMOTE2	2

/* Chip-specific registers */
#define TMP411_LOCAL			0x00
#define TMP411_REMOTE1			0x01
#define TMP411_STATUS_R			0x02
#define TMP411_CONFIGURATION1_R		0x03
#define TMP411_CONVERSION_RATE_R	0x04
#define TMP411_LOCAL_HIGH_LIMIT_R	0x05
#define TMP411_LOCAL_LOW_LIMIT_R	0x06
#define TMP411_REMOTE1_HIGH_LIMIT_R	0x07
#define TMP411_REMOTE1_LOW_LIMIT_R	0x08
#define TMP411_CONFIGURATION1_W		0x09
#define TMP411_CONVERSION_RATE_W	0x0a
#define TMP411_LOCAL_HIGH_LIMIT_W	0x0b
#define TMP411_LOCAL_LOW_LIMIT_W	0x0c
#define TMP411_REMOTE1_HIGH_LIMIT_W	0x0d
#define TMP411_REMOTE1_LOW_LIMIT_W	0x0e
#define TMP411_ONESHOT			0x0f
#define TMP411_REMOTE1_EXTD		0x10
#define TMP411_REMOTE1_HIGH_LIMIT_EXTD	0x13
#define TMP411_REMOTE1_LOW_LIMIT_EXTD	0x14
#define TMP411_REMOTE2_HIGH_LIMIT_R	0x15
#define TMP411_REMOTE2_HIGH_LIMIT_W	0x15
#define TMP411_REMOTE2_LOW_LIMIT_R	0x16
#define TMP411_REMOTE2_LOW_LIMIT_W	0x16
#define TMP411_REMOTE2_HIGH_LIMIT_EXTD	0x17
#define TMP411_REMOTE2_LOW_LIMIT_EXTD	0x18
#define TMP411_REMOTE1_THERM_LIMIT	0x19
#define TMP411_REMOTE2_THERM_LIMIT	0x1a
#define TMP411_STATUS_FAULT		0x1b
#define TMP411_CHANNEL_MASK		0x1f
#define TMP411_LOCAL_THERM_LIMIT	0x20
#define TMP411_THERM_HYSTERESIS		0x21
#define TMP411_CONSECUTIVE_ALERT	0x22
#define TMP411_REMOTE2			0x23
#define TMP411_REMOTE2_EXTD		0x24
#define TMP411_BETA_RANGE_CH1		0x25
#define TMP411_BETA_RANGE_CH2		0x26
#define TMP411_NFACTOR_REMOTE1		0x27
#define TMP411_NFACTOR_REMOTE2		0x28
#define TMP411_LOCAL_EXTD		0x29
#define TMP411_STATUS_LIMIT_HIGH	0x35
#define TMP411_STATUS_LIMIT_LOW		0x36
#define TMP411_STATUS_THERM		0x37
#define TMP411_RESET_W			0xfc
#define TMP411_MANUFACTURER_ID		0xfe
#define TMP411_DEVICE_ID		0xff

#define TMP411A_DEVICE_ID_VAL		0x12
#define TMP411B_DEVICE_ID_VAL		0x13
#define TMP411C_DEVICE_ID_VAL		0x10
#define TMP411d_DEVICE_ID_VAL		0x12

/* Config register bits */
#define TMP411_CONFIG1_TEMP_RANGE	BIT(2)
/* TMP411_CONFIG1_MODE bit is use to enable THERM mode */
#define TMP411_CONFIG1_MODE		BIT(5)
#define TMP411_CONFIG1_RUN_L		BIT(6)
#define TMP411_CONFIG1_ALERT_MASK_L	BIT(7)

/* Status register bits */
#define TMP411_STATUS_TEMP_THERM_ALARM	BIT(1)
#define TMP411_STATUS_OPEN		BIT(2)
#define TMP411_STATUS_TEMP_LOW_ALARM	BIT(3)
#define TMP411_STATUS_TEMP_HIGH_ALARM	BIT(4)
#define TMP411_STATUS_LOCAL_TEMP_LOW_ALARM	BIT(5)
#define TMP411_STATUS_LOCAL_TEMP_HIGH_ALARM	BIT(6)
#define TMP411_STATUS_BUSY		BIT(7)

/* Limits */
#define TMP411_HYSTERESIS_HIGH_LIMIT	255
#define TMP411_HYSTERESIS_LOW_LIMIT	0

enum tmp411_power_state {
	TMP411_POWER_OFF = 0,
	TMP411_POWER_ON,
	TMP411_POWER_COUNT
};

enum tmp411_channel_id {
	TMP411_CHANNEL_LOCAL,
	TMP411_CHANNEL_REMOTE1,

	TMP411_CHANNEL_COUNT
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
int tmp411_get_val(int idx, int *temp_ptr);

/**
 * Power control function of tmp411 temperature sensor.
 *
 * @param power_on	TMP411_POWER_ON: turn tmp411 sensor on.
 *			TMP411_POWER_OFF: shut tmp411 sensor down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp411_set_power(enum tmp411_power_state power_on);

/*
 * Set TMP411 ALERT#/THERM2# pin to THERM mode, and give a limit
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
int tmp411_set_therm_limit(int channel, int limit_c, int hysteresis);
#endif /* __CROS_EC_TMP411_H */
