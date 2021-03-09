/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TCPC driver for Renesas RAA489000 Buck-boost charger with TCPC
 */

#include "compile_time_macros.h"
#include "usb_pd_tcpm.h"

#ifndef __CROS_EC_USB_PD_TCPM_RAA489000_H
#define __CROS_EC_USB_PD_TCPM_RAA489000_H

#define RAA489000_TCPC0_I2C_FLAGS 0x22
#define RAA489000_TCPC1_I2C_FLAGS 0x23
#define RAA489000_TCPC2_I2C_FLAGS 0x24
#define RAA489000_TCPC3_I2C_FLAGS 0x25

/* Vendor registers */
#define RAA489000_TCPC_SETTING1			0x80
#define RAA489000_VBUS_VOLTAGE_TARGET		0x90
#define RAA489000_VBUS_CURRENT_TARGET		0x92
#define RAA489000_VBUS_OCP_UV_THRESHOLD		0x94
#define RAA489000_TYPEC_SETTING1		0xC0
#define RAA489000_PD_PHYSICAL_SETTING1		0xE0
#define RAA489000_PD_PHYSICAL_PARAMETER1	0xE8

/* TCPC_SETTING_1 */
#define RAA489000_TCPCV1_0_EN		BIT(0)
#define RAA489000_TCPC_PWR_CNTRL	BIT(4)

/* VBUS_CURRENT_TARGET */
#define RAA489000_VBUS_CURRENT_TARGET_3A	0x66 /* 3.0A + iOvershoot */
#define RAA489000_VBUS_CURRENT_TARGET_1_5A	0x38 /* 1.5A + iOvershoot */

/* VBUS_VOLTAGE_TARGET */
#define RAA489000_VBUS_VOLTAGE_TARGET_5160MV	0x102 /* 5.16V */
#define RAA489000_VBUS_VOLTAGE_TARGET_5220MV	0x105 /* 5.22V */

/* VBUS_OCP_UV_THRESHOLD */
/* Detect voltage level of overcurrent protection during Sourcing VBUS */
#define RAA489000_OCP_THRESHOLD_VALUE 0x00BE /* 4.75V */

/* TYPEC_SETTING1 - only older silicon */
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

/* PD_PHYSICAL_SETTING_1 */
#define RAA489000_PD_PHY_SETTING1_RECEIVER_EN BIT(9)
#define RAA489000_PD_PHY_SETTING1_SQUELCH_EN  BIT(8)
#define RAA489000_PD_PHY_SETTING1_TX_LDO11_EN BIT(0)

/* PD_PHYSICAL_PARMETER_1 */
#define PD_PHY_PARAM1_NOISE_FILTER_CNT_MASK (GENMASK(4, 0))

/**
 *
 * Set output current limit on the TCPC.  Note, this chip also offers an OTG
 * current level register in the charger i2c page but we must use the TCPC
 * current limit because the TCPC is controlling Vbus.
 *
 * @param port	USB-C port number
 * @param rp	Rp value for current limit (either 1.5A or 3A)
 *
 * @return	Zero if the current limit set was successful, non-zero otherwise
 */
int raa489000_set_output_current(int port, enum tcpc_rp_value rp);

extern const struct tcpm_drv raa489000_tcpm_drv;

#endif
