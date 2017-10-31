/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE IT5205 Type-C USB alternate mode mux.
 */

#ifndef __CROS_EC_USB_MUX_IT5205_H
#define __CROS_EC_USB_MUX_IT5205_H

/* 8 bit i2c slave address is 0xb0 or 0x90 depends on address setting pin. */

/* Chip ID registers */
#define IT5205_REG_CHIP_ID3 0x4
#define IT5205_REG_CHIP_ID2 0x5
#define IT5205_REG_CHIP_ID1 0x6
#define IT5205_REG_CHIP_ID0 0x7

/* MUX power down register */
#define IT5205_REG_MUXPDR        0x10
#define IT5205_MUX_POWER_DOWN    (1 << 0)

/* MUX control register */
#define IT5205_REG_MUXCR         0x11
#define IT5205_POLARITY_INVERTED (1 << 4)

#define IT5205_DP_USB_CTRL_MASK  0x0f
#define IT5205_DP                0x0f
#define IT5205_DP_USB            0x03
#define IT5205_USB               0x07

#endif /* __CROS_EC_USB_MUX_IT5205_H */
