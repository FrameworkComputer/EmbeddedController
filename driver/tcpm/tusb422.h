/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI TUSB422 Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_TUSB422_H
#define __CROS_EC_USB_PD_TCPM_TUSB422_H

/* I2C interface */
#define TUSB422_I2C_ADDR_FLAGS 0x20

#define TUSB422_REG_VENDOR_INTERRUPTS_STATUS 0x90
#define TUSB422_REG_VENDOR_INTERRUPTS_STATUS_FRS_RX BIT(0)

#define TUSB422_REG_VENDOR_INTERRUPTS_MASK 0x92
#define TUSB422_REG_VENDOR_INTERRUPTS_MASK_FRS_RX BIT(0)

#define TUSB422_REG_CC_GEN_CTRL 0x94
#define TUSB422_REG_CC_GEN_CTRL_GLOBAL_SW_RST BIT(5)

#define TUSB422_REG_PHY_BMC_RX_CTRL 0x96
#define TUSB422_REG_PHY_BMC_RX_CTRL_FRS_RX_EN BIT(3)

extern const struct tcpm_drv tusb422_tcpm_drv;

#endif /* defined(__CROS_EC_USB_PD_TCPM_TUSB422_H) */
