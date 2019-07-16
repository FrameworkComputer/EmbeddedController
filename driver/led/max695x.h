/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MAX6958/MAX6959 7-Segment LED Display Driver header
 */

#ifndef __CROS_EC_MAX656X_H
#define __CROS_EC_MAX656X_H

/* I2C interface */
#define MAX695X_I2C_ADDR1_FLAGS	0x38
#define MAX695X_I2C_ADDR2_FLAGS	0x39

/* Decode mode register */
#define MAX695X_REG_DECODE_MODE		0x01
/* Hexadecimal decode for digits 3–0 */
#define MAX695X_DECODE_MODE_HEX_DECODE	0x0f

/* Intensity register */
#define MAX695X_REG_INTENSITY		0x02
/* Setting meduim intensity */
#define MAX695X_INTENSITY_MEDIUM	0x20

/* Scan limit register value */
#define MAX695X_REG_SCAN_LIMIT		0x03

/* Scanning digits 0-3 */
#define MAX695X_SCAN_LIMIT_4		0x03

/* Configuration register */
#define MAX695X_REG_CONFIG		0x04
/* Shutdown seven segment display */
#define MAX695X_CONFIG_OPR_SHUTDOWN	0x00
/* Start seven segment display */
#define MAX695X_CONFIG_OPR_NORMAL	0x01

/* Digit addresses */
#define MAX695X_DIGIT0_ADDR	0x20
#define MAX695X_DIGIT1_ADDR	0x21
#define MAX695X_DIGIT2_ADDR	0x22
#define MAX695X_DIGIT3_ADDR	0x23

#endif /* __CROS_EC_MAX656X_H */
