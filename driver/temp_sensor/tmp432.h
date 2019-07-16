/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP432 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TMP432_H
#define __CROS_EC_TMP432_H

#define TMP432_I2C_ADDR_FLAGS		0x4C

#define TMP432_IDX_LOCAL	0
#define TMP432_IDX_REMOTE1	1
#define TMP432_IDX_REMOTE2	2
#define TMP432_IDX_COUNT	3

/* Chip-specific registers */
#define TMP432_LOCAL			0x00
#define TMP432_REMOTE1			0x01
#define TMP432_STATUS			0x02
#define TMP432_CONFIGURATION1_R		0x03
#define TMP432_CONVERSION_RATE_R	0x04
#define TMP432_LOCAL_HIGH_LIMIT_R	0x05
#define TMP432_LOCAL_LOW_LIMIT_R	0x06
#define TMP432_REMOTE1_HIGH_LIMIT_R	0x07
#define TMP432_REMOTE1_LOW_LIMIT_R	0x08
#define TMP432_CONFIGURATION1_W		0x09
#define TMP432_CONVERSION_RATE_W	0x0a
#define TMP432_LOCAL_HIGH_LIMIT_W	0x0b
#define TMP432_LOCAL_LOW_LIMIT_W	0x0c
#define TMP432_REMOTE1_HIGH_LIMIT_W	0x0d
#define TMP432_REMOTE1_LOW_LIMIT_W	0x0e
#define TMP432_ONESHOT			0x0f
#define TMP432_REMOTE1_EXTD		0x10
#define TMP432_REMOTE1_HIGH_LIMIT_EXTD	0x13
#define TMP432_REMOTE1_LOW_LIMIT_EXTD	0x14
#define TMP432_REMOTE2_HIGH_LIMIT_R	0x15
#define TMP432_REMOTE2_HIGH_LIMIT_W	0x15
#define TMP432_REMOTE2_LOW_LIMIT_R	0x16
#define TMP432_REMOTE2_LOW_LIMIT_W	0x16
#define TMP432_REMOTE2_HIGH_LIMIT_EXTD	0x17
#define TMP432_REMOTE2_LOW_LIMIT_EXTD	0x18
#define TMP432_REMOTE1_THERM_LIMIT	0x19
#define TMP432_REMOTE2_THERM_LIMIT	0x1a
#define TMP432_STATUS_FAULT		0x1b
#define TMP432_CHANNEL_MASK		0x1f
#define TMP432_LOCAL_THERM_LIMIT	0x20
#define TMP432_THERM_HYSTERESIS		0x21
#define TMP432_CONSECUTIVE_ALERT	0x22
#define TMP432_REMOTE2			0x23
#define TMP432_REMOTE2_EXTD		0x24
#define TMP432_BETA_RANGE_CH1		0x25
#define TMP432_BETA_RANGE_CH2		0x26
#define TMP432_NFACTOR_REMOTE1		0x27
#define TMP432_NFACTOR_REMOTE2		0x28
#define TMP432_LOCAL_EXTD		0x29
#define TMP432_STATUS_LIMIT_HIGH	0x35
#define TMP432_STATUS_LIMIT_LOW		0x36
#define TMP432_STATUS_THERM		0x37
#define TMP432_LOCAL_HIGH_LIMIT_EXTD	0x3d
#define TMP432_LOCAL_LOW_LIMIT_EXTD	0x3e
#define TMP432_CONFIGURATION2_R		0x3f
#define TMP432_CONFIGURATION2_W		0x3f
#define TMP432_RESET_W			0xfc
#define TMP432_DEVICE_ID		0xfd
#define TMP432_MANUFACTURER_ID		0xfe

/* Config register bits */
#define TMP432_CONFIG1_TEMP_RANGE	BIT(2)
/* TMP432_CONFIG1_MODE bit is use to enable THERM mode */
#define TMP432_CONFIG1_MODE		BIT(5)
#define TMP432_CONFIG1_RUN_L		BIT(6)
#define TMP432_CONFIG1_ALERT_MASK_L	BIT(7)
#define TMP432_CONFIG2_RESISTANCE_CORRECTION	BIT(2)
#define TMP432_CONFIG2_LOCAL_ENABLE	BIT(3)
#define TMP432_CONFIG2_REMOTE1_ENABLE	BIT(4)
#define TMP432_CONFIG2_REMOTE2_ENABLE	BIT(5)

/* Status register bits */
#define TMP432_STATUS_TEMP_THERM_ALARM	BIT(1)
#define TMP432_STATUS_OPEN		BIT(2)
#define TMP432_STATUS_TEMP_LOW_ALARM	BIT(3)
#define TMP432_STATUS_TEMP_HIGH_ALARM	BIT(4)
#define TMP432_STATUS_BUSY		BIT(7)

/* Limintaions */
#define TMP432_HYSTERESIS_HIGH_LIMIT	255
#define TMP432_HYSTERESIS_LOW_LIMIT	0

enum tmp432_power_state {
	TMP432_POWER_OFF = 0,
	TMP432_POWER_ON,
	TMP432_POWER_COUNT
};

enum tmp432_channel_id {
	TMP432_CHANNEL_LOCAL,
	TMP432_CHANNEL_REMOTE1,
	TMP432_CHANNEL_REMOTE2,

	TMP432_CHANNEL_COUNT
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
int tmp432_get_val(int idx, int *temp_ptr);

/**
 * Power control function of tmp432 temperature sensor.
 *
 * @param power_on	TMP432_POWER_ON: turn tmp432 sensor on.
 *			TMP432_POWER_OFF: shut tmp432 sensor down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp432_set_power(enum tmp432_power_state power_on);

/*
 * Set TMP432 ALERT#/THERM2# pin to THERM mode, and give a limit
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
int tmp432_set_therm_limit(int channel, int limit_c, int hysteresis);
#endif /* __CROS_EC_TMP432_H */
