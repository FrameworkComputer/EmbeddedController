/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nuvoton Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_NCT38XX_H
#define __CROS_EC_USB_PD_TCPM_NCT38XX_H

/* Chip variant ID (Part number)  */
#define NCT38XX_VARIANT_MASK               0x1C
#define NCT38XX_VARIANT_3807               0x0
#define NCT38XX_VARIANT_3808               0x2

#define NCT38XX_SUPPORT_GPIO_FLAGS (GPIO_OPEN_DRAIN | GPIO_INPUT | \
		GPIO_OUTPUT | GPIO_LOW | GPIO_HIGH)

/* I2C interface */
#define NCT38xx_I2C_ADDR1_1_FLAGS          0x70
#define NCT38xx_I2C_ADDR1_2_FLAGS          0x71
#define NCT38xx_I2C_ADDR1_3_FLAGS          0x72
#define NCT38xx_I2C_ADDR1_4_FLAGS          0x73

#define NCT38xx_I2C_ADDR2_1_FLAGS          0x74
#define NCT38xx_I2C_ADDR2_2_FLAGS          0x75
#define NCT38xx_I2C_ADDR2_3_FLAGS          0x76
#define NCT38xx_I2C_ADDR2_4_FLAGS          0x77

#define NCT38XX_REG_VENDOR_ID_L            0x00
#define NCT38XX_REG_VENDOR_ID_H            0x01
#define NCT38XX_VENDOR_ID                  0x0416

#define NCT38XX_PRODUCT_ID                 0xC301

#define NCT38XXX_REG_GPIO_DATA_IN(n)       (0xC0 + ((n) * 8))
#define NCT38XXX_REG_GPIO_DATA_OUT(n)      (0xC1 + ((n) * 8))
#define NCT38XXX_REG_GPIO_DIR(n)           (0xC2 + ((n) * 8))
#define NCT38XXX_REG_GPIO_OD_SEL(n)        (0xC3 + ((n) * 8))
#define NCT38XXX_REG_MUX_CONTROL            0xD0

/* NCT3808 only supports GPIO 2/3/4/6/7 */
#define NCT38XXX_3808_VALID_GPIO_MASK            0xDC

#define NCT38XX_REG_CTRL_OUT_EN            0xD2
#define NCT38XX_REG_CTRL_OUT_EN_SRCEN      (1 << 0)
#define NCT38XX_REG_CTRL_OUT_EN_FASTEN     (1 << 1)
#define NCT38XX_REG_CTRL_OUT_EN_SNKEN      (1 << 2)
#define NCT38XX_REG_CTRL_OUT_EN_CONNDIREN  (1 << 6)

extern const struct tcpm_drv nct38xx_tcpm_drv;

#endif /* defined(__CROS_EC_USB_PD_TCPM_NCT38XX_H) */
