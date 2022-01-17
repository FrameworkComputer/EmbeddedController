/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_LED_AW20198_H
#define __CROS_EC_DRIVER_LED_AW20198_H

/* This depends on AD0 and AD1. (GRD, GRD) = 0x20. */
#define AW20198_I2C_ADDR_FLAG	0x20

#define AW20198_ROW_SIZE	6
#define AW20198_COL_SIZE	11
#define AW20198_GRID_SIZE	(AW20198_COL_SIZE * AW20198_ROW_SIZE)

#define AW20198_PAGE_FUNC	0xC0
#define AW20198_PAGE_PWM	0xC1
#define AW20198_PAGE_SCALE	0xC2

#define AW20198_REG_GCR		0x00
#define AW20198_REG_GCC		0x01
#define AW20198_REG_RSTN	0x2F
#define AW20198_REG_MIXCR	0x46
#define AW20198_REG_PAGE	0xF0

#define AW20198_RESET_MAGIC	0xAE

extern const struct rgbkbd_drv aw20198_drv;

#endif  /* __CROS_EC_DRIVER_LED_AW20198_H */
