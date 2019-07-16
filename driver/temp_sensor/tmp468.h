/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TMP468 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TMP468_H
#define __CROS_EC_TMP468_H

#define TMP468_I2C_ADDR_FLAGS (0x48 | I2C_FLAG_BIG_ENDIAN)
#define TMP468_SHIFT1 7

#define TMP468_LOCAL			0x00
#define TMP468_REMOTE1			0x01
#define TMP468_REMOTE2			0x02
#define TMP468_REMOTE3			0x03
#define TMP468_REMOTE4			0x04
#define TMP468_REMOTE5			0x05
#define TMP468_REMOTE6			0x06
#define TMP468_REMOTE7			0x07
#define TMP468_REMOTE8			0x08

#define TMP468_SRST			0x20
#define TMP468_THERM			0x21
#define TMP468_THERM2			0x22
#define TMP468_ROPEN			0x23

#define TMP468_CONFIGURATION		0x30
#define TMP468_THERM_HYST		0x38

#define TMP468_LOCAL_LOW_LIMIT		0x39
#define TMP468_LOCAL_HIGH_LIMT		0x3a

#define TMP468_REMOTE1_OFFSET		0x40
#define TMP468_REMOTE1_NFACTOR		0x41
#define TMP468_REMOTE1_LOW_LIMIT	0x41
#define TMP468_REMOTE1_HIGH_LIMIT	0x42

#define TMP468_REMOTE2_OFFSET		0x48
#define TMP468_REMOTE2_NFACTOR		0x49
#define TMP468_REMOTE2_LOW_LIMIT	0x4a
#define TMP468_REMOTE2_HIGH_LIMIT	0x4b

#define TMP468_REMOTE3_OFFSET		0x50
#define TMP468_REMOTE3_NFACTOR		0x51
#define TMP468_REMOTE3_LOW_LIMIT	0x52
#define TMP468_REMOTE3_HIGH_LIMIT	0x53

#define TMP468_REMOTE4_OFFSET		0x58
#define TMP468_REMOTE4_NFACTOR		0x59
#define TMP468_REMOTE4_LOW_LIMIT	0x59
#define TMP468_REMOTE4_HIGH_LIMIT	0x5a

#define TMP468_REMOTE5_OFFSET		0x60
#define TMP468_REMOTE5_NFACTOR		0x61
#define TMP468_REMOTE5_LOW_LIMIT	0x62
#define TMP468_REMOTE5_HIGH_LIMIT	0x63

#define TMP468_REMOTE6_OFFSET		0x68
#define TMP468_REMOTE6_NFACTOR		0x69
#define TMP468_REMOTE6_LOW_LIMIT	0x6a
#define TMP468_REMOTE6_HIGH_LIMIT	0x6b

#define TMP468_REMOTE7_OFFSET		0x70
#define TMP468_REMOTE7_NFACTOR		0x71
#define TMP468_REMOTE7_LOW_LIMIT	0x72
#define TMP468_REMOTE7_HIGH_LIMIT	0x73

#define TMP468_REMOTE8_OFFSET		0x78
#define TMP468_REMOTE8_NFACTOR		0x79
#define TMP468_REMOTE8_LOW_LIMIT	0x7a
#define TMP468_REMOTE8_HIGH_LIMIT	0x7b

#define TMP468_LOCK			0xc4

#define TMP468_DEVICE_ID		0xfd
#define TMP468_MANUFACTURER_ID		0xfe

#define TMP468_SHUTDOWN			BIT(5)

enum tmp468_channel_id {
	TMP468_CHANNEL_LOCAL,

	TMP468_CHANNEL_REMOTE1,
	TMP468_CHANNEL_REMOTE2,
	TMP468_CHANNEL_REMOTE3,
	TMP468_CHANNEL_REMOTE4,
	TMP468_CHANNEL_REMOTE5,
	TMP468_CHANNEL_REMOTE6,
	TMP468_CHANNEL_REMOTE7,
	TMP468_CHANNEL_REMOTE8,

	TMP468_CHANNEL_COUNT
};

enum tmp468_power_state {
	TMP468_POWER_OFF = 0,
	TMP468_POWER_ON,

	TMP468_POWER_COUNT
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
int tmp468_get_val(int idx, int *temp_ptr);

/**
 * Power control function of tmp432 temperature sensor.
 *
 * @param power_on	TMP468_POWER_ON: turn tmp468 sensor on.
 *			TMP468_POWER_OFF: shut tmp468 sensor down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp468_set_power(enum tmp468_power_state power_on);

#endif /* __CROS_EC_TMP468_H */
