/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * SB-TSI: SB Temperature Sensor Interface.
 * This is an I2C slave temp sensor on the AMD Stony Ridge FT4 SOC.
 */

#ifndef __CROS_EC_SB_TSI_H
#define __CROS_EC_SB_TSI_H

#define SB_TSI_I2C_ADDR_FLAGS		0x4C

/* G781 register */
#define SB_TSI_TEMP_H			0x01
#define SB_TSI_STATUS			0x02
#define SB_TSI_CONFIG_1			0x03
#define SB_TSI_UPDATE_RATE		0x04
#define SB_TSI_HIGH_TEMP_THRESHOLD_H	0x07
#define SB_TSI_LOW_TEMP_THRESHOLD_H	0x08
#define SB_TSI_CONFIG_2			0x09
#define SB_TSI_TEMP_L			0x10
#define SB_TSI_TEMP_OFFSET_H		0x11
#define SB_TSI_TEMP_OFFSET_L		0x12
#define SB_TSI_HIGH_TEMP_THRESHOLD_L	0x13
#define SB_TSI_LOW_TEMP_THRESHOLD_L	0x14
#define SB_TSI_TIMEOUT_CONFIG		0x22
#define SB_TSI_PSTATE_LIMIT_CONFIG	0x2F
#define SB_TSI_ALERT_THRESHOLD		0x32
#define SB_TSI_ALERT_CONFIG		0xBF
#define SB_TSI_MANUFACTURE_ID		0xFE
#define SB_TSI_REVISION			0xFF

/**
 * Get the value of a sensor in K.
 *
 * @param idx		Index to read. Only 0 is valid for sb_tsi.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int sb_tsi_get_val(int idx, int *temp_ptr);

#endif  /* __CROS_EC_SB_TSI_H */
