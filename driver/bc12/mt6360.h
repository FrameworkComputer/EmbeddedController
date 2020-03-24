/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MT6360_H

#define MT6360_PMU_SLAVE_ADDR_FLAGS 0x34
#define MT6360_PMIC_SLAVE_ADDR_FLAGS 0x1A
#define MT6360_LDO_SLAVE_ADDR_FLAGS 0x64
#define MT6360_PD_SLAVE_ADDR_FLAGS 0x4E

#define MT6360_IRQ_MASK 0x0C

#define MT6360_REG_DEVICE_TYPE 0x22
#define MT6360_MASK_USBCHGEN BIT(7)

#define MT6360_REG_USB_STATUS_1 0x27
#define MT6360_MASK_USB_STATUS 0x70
#define MT6360_MASK_SDP 0x20
#define MT6360_MASK_DCP 0x40
#define MT6360_MASK_CDP 0x50

#define MT6360_REG_DPDMIRQ 0xD6
#define MT6360_MASK_DPDMIRQ_ATTACH BIT(0)
#define MT6360_MASK_DPDMIRQ_DETACH BIT(1)

#define MT6360_REG_DPDM_MASK1 0xF6
#define MT6360_REG_DPDM_MASK1_CHGDET_DONEI_M BIT(0)

struct mt6360_config_t {
	int i2c_port;
	int i2c_addr_flags;
};

extern const struct mt6360_config_t mt6360_config;

extern const struct bc12_drv mt6360_drv;

#endif /* __CROS_EC_MT6360_H */
