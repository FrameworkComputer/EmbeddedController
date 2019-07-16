/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3509 LED driver.
 */

#ifndef __CROS_EC_LM3509_H
#define __CROS_EC_LM3509_H

#define LM3509_I2C_ADDR_FLAGS	0x36

/*
 * General purpose register
 *
 * [2]= set both main and secondary current same
 *      both control by BMAIN.
 * [1]= enable secondary current sink.
 * [0]= enable main current sink.
 */
#define LM3509_REG_GP		0x10

/*
 * Brightness register
 *
 * 0x00: 0%
 * 0x1F: 100%
 * Power-on-value: 0% (0xE0)
 */
#define LM3509_REG_BMAIN	0xA0
#define LM3509_REG_BSUB		0xB0

#define LM3509_BMAIN_MASK	0x1F

extern const struct kblight_drv kblight_lm3509;

#endif /* __CROS_EC_LM3509_H */
