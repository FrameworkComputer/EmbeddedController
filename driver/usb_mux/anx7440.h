/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Analogix ANX7440 USB Type-C Active mux with
 * Integrated Re-timers for USB3.1/DisplayPort.
 */

#ifndef __CROS_EC_USB_MUX_ANX7440_H
#define __CROS_EC_USB_MUX_ANX7440_H

/* I2C interface address */
#define ANX7440_I2C_ADDR1_FLAGS 0x10
#define ANX7440_I2C_ADDR2_FLAGS 0x12
#define I2C_ADDR_USB_MUX0_FLAGS ANX7440_I2C_ADDR1_FLAGS
#define I2C_ADDR_USB_MUX1_FLAGS ANX7440_I2C_ADDR2_FLAGS

/* Vendor / Device Id registers and expected fused values */
#define ANX7440_REG_VENDOR_ID_L    0x00
#define ANX7440_VENDOR_ID_L        0xaa
#define ANX7440_REG_VENDOR_ID_H    0x01
#define ANX7440_VENDOR_ID_H        0xaa
#define ANX7440_REG_DEVICE_ID_L    0x02
#define ANX7440_DEVICE_ID_L        0x40
#define ANX7440_REG_DEVICE_ID_H    0x03
#define ANX7440_DEVICE_ID_H        0x74
#define ANX7440_REG_DEVICE_VERSION 0x04
#define ANX7440_DEVICE_VERSION     0xCB

/* Chip control register for checking mux state */
#define ANX7440_REG_CHIP_CTRL               0x05
#define ANX7440_CHIP_CTRL_FINAL_FLIP        BIT(6)
#define ANX7440_CHIP_CTRL_OP_MODE_FINAL_DP  BIT(5)
#define ANX7440_CHIP_CTRL_OP_MODE_FINAL_USB BIT(4)
#define ANX7440_CHIP_CTRL_SW_FLIP           BIT(2)
#define ANX7440_CHIP_CTRL_SW_OP_MODE_DP     BIT(1)
#define ANX7440_CHIP_CTRL_SW_OP_MODE_USB    BIT(0)
#define ANX7440_CHIP_CTRL_SW_OP_MODE_CLEAR  0x7

#endif /* __CROS_EC_USB_MUX_ANX7440_H */
