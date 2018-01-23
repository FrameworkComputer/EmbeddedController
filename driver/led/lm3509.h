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

/* Power on and initialize LM3509. */
int lm3509_poweron(void);

/* Power off LM3509. */
int lm3509_poweroff(void);

#endif /* __CROS_EC_LM3509_H */
