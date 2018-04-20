/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3509 LED driver.
 */

#ifndef __CROS_EC_LM3509_H
#define __CROS_EC_LM3509_H

/* 8-bit I2C address */
#define LM3509_I2C_ADDR		0x6C

#define LM3509_REG_GP		0x10
#define LM3509_REG_BMAIN	0xA0
#define LM3509_REG_BSUB		0xB0

#define LM3509_BMAIN_MASK	0x1F

/**
 * Power on/off and initialize LM3509.
 *
 * @param enable: 1 to enable or 0 to disable.
 * @return EC_SUCCESS or EC_ERROR_* on error.
 */
int lm3509_power(int enable);

/**
 * Set brightness level
 *
 * @param percent: Brightness level: 0 - 100%
 * @return EC_SUCCESS or EC_ERROR_* on error.
 */
int lm3509_set_brightness(int percent);

/**
 * Get current brightness level
 *
 * @param percent: Current brightness level.
 * @return EC_SUCCESS or EC_ERROR_* on error.
 */
int lm3509_get_brightness(int *percent);

#endif /* __CROS_EC_LM3509_H */
