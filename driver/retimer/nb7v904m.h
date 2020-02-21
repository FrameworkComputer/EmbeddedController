/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ON Semiconductor NB7V904M USB Type-C DisplayPort Alt Mode Redriver
 */

#ifndef __CROS_EC_USB_REDRIVER_NB7V904M_H
#define __CROS_EC_USB_REDRIVER_NB7V904M_H

#include "compile_time_macros.h"
#include "usb_mux.h"

#define NB7V904M_I2C_ADDR0 0x19
#define NB7V904M_I2C_ADDR1 0x1A
#define NB7V904M_I2C_ADDR2 0x1C

/* Registers */
#define NB7V904M_REG_GEN_DEV_SETTINGS	0x00
#define NB7V904M_REG_AUX_CH_CTRL        0x09

/* 0x00 - General Device Settings */
#define NB7V904M_CHIP_EN        BIT(0)
#define NB7V904M_USB_DP_NORMAL  BIT(1)
#define NB7V904M_USB_DP_FLIPPED 0
#define NB7V904M_DP_ONLY        BIT(2)
#define NB7V904M_USB_ONLY       (BIT(3) | BIT(1))
#define NB7V904M_OP_MODE_MASK   GENMASK(3, 1)
#define NB7V904M_CH_A_EN        BIT(4)
#define NB7V904M_CH_B_EN        BIT(5)
#define NB7V904M_CH_C_EN        BIT(6)
#define NB7V904M_CH_D_EN        BIT(7)
#define NB7V904M_CH_EN_MASK     GENMASK(7, 4)

/* 0x09 - Auxiliary Channel Control */
#define NB7V904M_AUX_CH_NORMAL   0
#define NB7V904M_AUX_CH_FLIPPED  BIT(0)
#define NB7V904M_AUX_CH_HI_Z     BIT(1)

extern const struct usb_mux_driver nb7v904m_usb_redriver_drv;
#endif /* __CROS_EC_USB_REDRIVER_NB7V904M_H */
