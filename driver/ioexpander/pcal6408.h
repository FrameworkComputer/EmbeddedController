/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA(L)6408 I/O expander
 */

#ifndef __CROS_EC_IOEXPANDER_PCAL6408_H
#define __CROS_EC_IOEXPANDER_PCAL6408_H

#define PCAL6408_I2C_ADDR0		0x20
#define PCAL6408_I2C_ADDR1		0x21

#define PCAL6408_REG_INPUT		0x00
#define PCAL6408_REG_OUTPUT		0x01
#define PCAL6408_REG_POLARITY_INVERSION	0x02
#define PCAL6408_REG_CONFIG		0x03
#define PCAL6408_REG_OUT_STRENGTH0	0x40
#define PCAL6408_REG_OUT_STRENGTH1	0x41
#define PCAL6408_REG_INPUT_LATCH	0x42
#define PCAL6408_REG_PULL_ENABLE	0x43
#define PCAL6408_REG_PULL_UP_DOWN	0x44
#define PCAL6408_REG_INT_MASK		0x45
#define PCAL6408_REG_INT_STATUS		0x46
#define PCAL6408_REG_OUT_CONFIG		0x4f

#define PCAL6408_VALID_GPIO_MASK	0xff

#define PCAL6408_OUTPUT			0
#define PCAL6408_INPUT			1

#define PCAL6408_OUT_CONFIG_OPEN_DRAIN	0x01

/*
 * Check which IO's interrupt event is triggered. If any, call its
 * registered interrupt handler.
 */
int pcal6408_ioex_event_handler(int ioex);

extern const struct ioexpander_drv pcal6408_ioexpander_drv;

#endif  /* __CROS_EC_IOEXPANDER_PCAL6408_H */
