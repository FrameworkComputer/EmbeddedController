/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parade Tech Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_PS8XXX_H
#define __CROS_EC_USB_PD_TCPM_PS8XXX_H

#define PS8XXX_VENDOR_ID  0x1DA0

#if defined(CONFIG_USB_PD_TCPM_PS8751)
/* Vendor defined registers */
#define PS8XXX_PRODUCT_ID 0x8751

#define FW_VER_REG                              0x90
#define PS8XXX_REG_VENDOR_ID_L                  0x00
#define PS8XXX_REG_VENDOR_ID_H                  0x01
#define MUX_IN_HPD_ASSERTION_REG                0xD0
#define IN_HPD  (1 << 0)
#define HPD_IRQ (1 << 1)
#define PS8XXX_REG_MUX_DP_EQ_CONFIGURATION      0xD3
#define PS8XXX_REG_MUX_USB_C2SS_EQ              0xE7
#define PS8XXX_REG_MUX_USB_C2SS_HS_THRESHOLD    0xE8

#elif defined(CONFIG_USB_PD_TCPM_PS8805)
/* Vendor defined registers */
#define PS8XXX_PRODUCT_ID 0x8805

#define FW_VER_REG               0x82
#define MUX_IN_HPD_ASSERTION_REG 0xD0
#define IN_HPD  (1 << 0)
#define HPD_IRQ (1 << 1)

#endif

extern const struct tcpm_drv ps8xxx_tcpm_drv;
void ps8xxx_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq);
int ps8xxx_tcpc_get_fw_version(int port, int *version);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
extern struct i2c_stress_test_dev ps8xxx_i2c_stress_test_dev;
#endif /* defined(CONFIG_CMD_I2C_STRESS_TEST_TCPC) */

#endif /* defined(__CROS_EC_USB_PD_TCPM_PS8XXX_H) */
