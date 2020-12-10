/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Richtek RT1715 Type-C port controller */
#ifndef __CROS_EC_USB_PD_TCPM_RT1715_H
#define __CROS_EC_USB_PD_TCPM_RT1715_H

/* I2C interface */
#define RT1715_I2C_ADDR_FLAGS                   0x4E

#define RT1715_VENDOR_ID                        0x29CF

#define RT1715_REG_VENDOR_7                     0xA0
#define RT1715_REG_VENDOR_7_SOFT_RESET          BIT(0)

#define RT1715_REG_PHY_CTRL1                    0x80

#define RT1715_REG_PHY_CTRL2                    0x81

#define RT1715_REG_BMCIO_RXDZSEL                0x93
#define RT1715_REG_BMCIO_RXDZSEL_OCCTRL_600MA   BIT(7)
#define RT1715_REG_BMCIO_RXDZSEL_MASK           BIT(0)

#define RT1715_REG_VENDOR_5                     0x9B
#define RT1715_REG_VENDOR_5_SHUTDOWN_OFF        BIT(5)
#define RT1715_REG_VENDOR_5_ENEXTMSG            BIT(4)

#define RT1715_REG_I2CRST_CTRL                  0x9E
/* I2C reset : (val + 1) * 12.5ms */
#define RT1715_REG_I2CRST_CTRL_TOUT_200MS       0x0F
#define RT1715_REG_I2CRST_CTRL_TOUT_150MS       0x0B
#define RT1715_REG_I2CRST_CTRL_TOUT_100MS       0x07
#define RT1715_REG_I2CRST_CTRL_EN               BIT(7)


#define RT1715_REG_TTCPC_FILTER                 0xA1
#define RT1715_REG_TTCPC_FILTER_400US           0x0F

#define RT1715_REG_DRP_TOGGLE_CYCLE             0xA2
#define RT1715_REG_DRP_TOGGLE_CYCLE_76MS        0x04

#define RT1715_REG_DRP_DUTY_CTRL                0xA3
#define RT1715_REG_DRP_DUTY_CTRL_40PERCENT      400

#define RT1715_REG_BMCIO_RXDZEN                 0xAF


extern const struct tcpm_drv rt1715_tcpm_drv;

#endif /* defined(__CROS_EC_USB_PD_TCPM_RT1715_H) */
