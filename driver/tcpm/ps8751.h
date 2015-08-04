/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parade Tech Type-C controller vendor specific APIs*/

#ifndef __CROS_EC_USB_PD_TCPM_PS8751_H
#define __CROS_EC_USB_PD_TCPM_PS8751_H

/* Vendor defined registers */
#define PS8751_VENDOR_ID	0x1DA0
#define PS8751_PRODUCT_ID	0x8751

#define PS8751_REG_VERSION      0x90
#define PS8751_REG_VENDOR_ID_L  0x00
#define PS8751_REG_VENDOR_ID_H  0x01
#define PS8751_REG_CTRL_1       0xD0
#define PS8751_REG_CTRL_1_HPD   (1 << 0)
#define PS8751_REG_CTRL_1_IRQ   (1 << 1)

void ps8751_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq);
int ps8751_tcpc_get_fw_version(int port, int *version);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
extern struct i2c_stress_test_dev ps8751_i2c_stress_test_dev;
#endif

#endif /* __CROS_EC_USB_PD_TCPM_PS8751_H */

