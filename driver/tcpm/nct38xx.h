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

/* There are two IO ports in NCT3807 */
#define NCT38XX_NCT3807_MAX_IO_PORT        2
/* There is only one IO port in NCT3808 */
#define NCT38XX_NCT3808_MAX_IO_PORT        1

#define NCT38XX_SUPPORT_GPIO_FLAGS (GPIO_OPEN_DRAIN | GPIO_INPUT | \
		GPIO_OUTPUT | GPIO_LOW | GPIO_HIGH | GPIO_INT_F_RISING | \
		GPIO_INT_F_FALLING | GPIO_INT_F_HIGH | GPIO_INT_F_LOW)

/* I2C interface */
#define NCT38XX_I2C_ADDR1_1_FLAGS          0x70
#define NCT38XX_I2C_ADDR1_2_FLAGS          0x71
#define NCT38XX_I2C_ADDR1_3_FLAGS          0x72
#define NCT38XX_I2C_ADDR1_4_FLAGS          0x73

#define NCT38XX_I2C_ADDR2_1_FLAGS          0x74
#define NCT38XX_I2C_ADDR2_2_FLAGS          0x75
#define NCT38XX_I2C_ADDR2_3_FLAGS          0x76
#define NCT38XX_I2C_ADDR2_4_FLAGS          0x77

#define NCT38XX_REG_VENDOR_ID_L            0x00
#define NCT38XX_REG_VENDOR_ID_H            0x01
#define NCT38XX_VENDOR_ID                  0x0416

#define NCT38XX_PRODUCT_ID                 0xC301

#define NCT38XX_REG_GPIO_DATA_IN(n)       (0xC0 + ((n) * 8))
#define NCT38XX_REG_GPIO_DATA_OUT(n)      (0xC1 + ((n) * 8))
#define NCT38XX_REG_GPIO_DIR(n)           (0xC2 + ((n) * 8))
#define NCT38XX_REG_GPIO_OD_SEL(n)        (0xC3 + ((n) * 8))
#define NCT38XX_REG_GPIO_ALERT_RISE(n)    (0xC4 + ((n) * 8))
#define NCT38XX_REG_GPIO_ALERT_FALL(n)    (0xC5 + ((n) * 8))
#define NCT38XX_REG_GPIO_ALERT_LEVEL(n)   (0xC6 + ((n) * 8))
#define NCT38XX_REG_GPIO_ALERT_MASK(n)    (0xC7 + ((n) * 8))
#define NCT38XX_REG_MUX_CONTROL            0xD0
#define NCT38XX_REG_GPIO_ALERT_STAT(n)    (0xD4 + (n))

/* NCT3808 only supports GPIO 2/3/4/6/7 */
#define NCT38XXX_3808_VALID_GPIO_MASK            0xDC

#define NCT38XX_REG_CTRL_OUT_EN            0xD2
#define NCT38XX_REG_CTRL_OUT_EN_SRCEN      (1 << 0)
#define NCT38XX_REG_CTRL_OUT_EN_FASTEN     (1 << 1)
#define NCT38XX_REG_CTRL_OUT_EN_SNKEN      (1 << 2)
#define NCT38XX_REG_CTRL_OUT_EN_CONNDIREN  (1 << 6)

#define NCT38XX_RESET_HOLD_DELAY_MS	1
#define NCT38XX_RESET_POST_DELAY_MS	0

extern const struct tcpm_drv nct38xx_tcpm_drv;

/*
 * The interrupt handler to handle Vendor Define ALERT event from IOEX chip.
 *
 * Normally, the Vendor Define event should be checked by the NCT38XX TCPCI
 * driver's tcpc_alert function.
 * This function is only included when NCT38XX TCPC driver is not included.
 * (i.e. CONFIG_USB_PD_TCPM_NCT38XX is not defined)
 */
void nct38xx_ioex_handle_alert(int ioex);

/*
 * Check which IO's interrupt event is triggered. If any, call its
 * registered interrupt handler.
 */
int nct38xx_ioex_event_handler(int ioex);

extern const struct ioexpander_drv nct38xx_ioexpander_drv;

#endif /* defined(__CROS_EC_USB_PD_TCPM_NCT38XX_H) */
