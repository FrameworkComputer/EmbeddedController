/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* G781 temperature sensor module for Chrome EC */

#ifndef __CROS_EC_TEMP_SENSOR_G781_H
#define __CROS_EC_TEMP_SENSOR_G781_H

#define G781_I2C_ADDR		0x98 /* 7-bit address is 0x4C */

#define G781_IDX_INTERNAL	0
#define G781_IDX_EXTERNAL	1

/* Chip-specific commands */
#define G781_TEMP_LOCAL			0x00
#define G781_TEMP_REMOTE		0x01
#define G781_STATUS			0x02
#define G781_CONFIGURATION_R		0x03
#define G781_CONVERSION_RATE_R		0x04
#define G781_LOCAL_TEMP_HIGH_LIMIT_R	0x05
#define G781_LOCAL_TEMP_LOW_LIMIT_R	0x06
#define G781_REMOTE_TEMP_HIGH_LIMIT_R	0x07
#define G781_REMOTE_TEMP_LOW_LIMIT_R	0x08
#define G781_CONFIGURATION_W		0x09
#define G781_CONVERSION_RATE_W		0x0a
#define G781_LOCAL_TEMP_HIGH_LIMIT_W	0x0b
#define G781_LOCAL_TEMP_LOW_LIMIT_W	0x0c
#define G781_REMOTE_TEMP_HIGH_LIMIT_W	0x0d
#define G781_REMOTE_TEMP_LOW_LIMIT_W	0x0e
#define G781_ONESHOT			0x0f
#define G781_REMOTE_TEMP_EXTENDED	0x10
#define G781_REMOTE_TEMP_OFFSET_HIGH	0x11
#define G781_REMOTE_TEMP_OFFSET_EXTD	0x12
#define G781_REMOTE_T_HIGH_LIMIT_EXTD	0x13
#define G781_REMOTE_T_LOW_LIMIT_EXTD	0x14
#define G781_REMOTE_TEMP_THERM_LIMIT	0x19
#define G781_LOCAL_TEMP_THERM_LIMIT	0x20
#define G781_THERM_HYSTERESIS		0x21
#define G781_ALERT_FAULT_QUEUE_CODE	0x22
#define G781_MANUFACTURER_ID		0xFE
#define G781_DEVICE_ID			0xFF

/* Config register bits */
#define G781_CONFIGURATION_STANDBY	(1 << 6)
#define G781_CONFIGURATION_ALERT_MASK	(1 << 7)

/* Status register bits */
#define G781_STATUS_LOCAL_TEMP_THERM_ALARM	(1 << 0)
#define G781_STATUS_REMOTE_TEMP_THERM_ALARM	(1 << 1)
#define G781_STATUS_REMOTE_TEMP_FAULT		(1 << 2)
#define G781_STATUS_REMOTE_TEMP_LOW_ALARM	(1 << 3)
#define G781_STATUS_REMOTE_TEMP_HIGH_ALARM	(1 << 4)
#define G781_STATUS_LOCAL_TEMP_LOW_ALARM	(1 << 5)
#define G781_STATUS_LOCAL_TEMP_HIGH_ALARM	(1 << 6)
#define G781_STATUS_BUSY			(1 << 7)

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read. Idx indicates whether to read die
 *			temperature or external temperature.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int g781_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_TEMP_SENSOR_G781_H */
