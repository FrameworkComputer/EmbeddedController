/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TCPC driver for Renesas RAA489000 Buck-boost charger with TCPC
 */

#ifndef __CROS_EC_USB_PD_TCPM_RAA489000_H
#define __CROS_EC_USB_PD_TCPM_RAA489000_H

#define RAA489000_TCPC0_I2C_FLAGS 0x22
#define RAA489000_TCPC1_I2C_FLAGS 0x23
#define RAA489000_TCPC2_I2C_FLAGS 0x24
#define RAA489000_TCPC3_I2C_FLAGS 0x25

/* Vendor registers */
#define RAA489000_TYPEC_SETTING1	0xC0

/* Enables for reverse current protection */
#define RAA489000_SETTING1_IP2_EN	BIT(9)
#define RAA489000_SETTING1_IP1_EN	BIT(8)

/* Switches from dead-battery Rd */
#define RAA489000_SETTING1_RDOE		BIT(7)

/* CC comparator enables */
#define RAA489000_SETTING1_CC2_CMP3_EN	BIT(6)
#define RAA489000_SETTING1_CC2_CMP2_EN	BIT(5)
#define RAA489000_SETTING1_CC2_CMP1_EN	BIT(4)
#define RAA489000_SETTING1_CC1_CMP3_EN	BIT(3)
#define RAA489000_SETTING1_CC1_CMP2_EN	BIT(2)
#define RAA489000_SETTING1_CC1_CMP1_EN	BIT(1)

/* CC debounce enable */
#define RAA489000_SETTING1_CC_DB_EN	BIT(0)

extern const struct tcpm_drv raa489000_tcpm_drv;

#endif
