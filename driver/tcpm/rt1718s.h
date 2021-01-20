/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_PD_TCPM_RT1718S_H
#define __CROS_EC_USB_PD_TCPM_RT1718S_H

#include "usb_pd_tcpm.h"

/* RT1718S Private RegMap */
#define RT1718S_SLAVE_ADDR_FLAGS			0x43

#define RT1718S_VID					0x29CF
#define RT1718S_PID					0x1718

#define RT1718S_PHYCTRL1				0x80
#define RT1718S_PHYCTRL2				0x81
#define RT1718S_PHYCTRL3				0x82
#define RT1718S_PHYCTRL7				0x86
#define RT1718S_VCON_CTRL1				0x8A
#define RT1718S_VCON_CTRL3				0x8C
#define RT1718S_SYS_CTRL1				0x8F
#define RT1718S_SYS_CTRL1_TCPC_CONN_INVALID		BIT(6)
#define RT1718S_SYS_CTRL1_SHIPPING_OFF			BIT(5)

#define RT1718S_RT_MASK1				0x91
#define RT1718S_RT_MASK1_M_VBUS_FRS_LOW			BIT(7)
#define RT1718S_RT_MASK1_M_RX_FRS			BIT(6)
#define RT1718S_RT_MASK2				0x92
#define RT1718S_RT_MASK3				0x93
#define RT1718S_RT_MASK4				0x94
#define RT1718S_RT_MASK5				0x95
#define RT1718S_RT_MASK6				0x96
#define RT1718S_RT_MASK7				0x97

#define RT1718S_PHYCTRL9				0xAC

#define RT1718S_SYS_CTRL3				0xB0
#define RT1718S_TCPC_CTRL1				0xB1
#define RT1718S_TCPC_CTRL2				0xB2
#define RT1718S_TCPC_CTRL3				0xB3
#define RT1718S_SWRESET_MASK				BIT(0)
#define RT1718S_TCPC_CTRL4				0xB4
#define RT1718S_SYS_CTRL4				0xB8
#define RT1718S_WATCHDOG_CTRL				0xBE
#define RT1718S_I2C_RST_CTRL				0xBF

#define RT1718S_HILO_CTRL9				0xC8
#define RT1718S_SHILED_CTRL1				0xCA

#define RT1718S_DIS_SRC_VBUS_CTRL			0xE0
#define RT1718S_ENA_SRC_VBUS_CTRL			0xE1
#define RT1718S_FAULT_OC1_VBUS_CTRL			0xE3
#define RT1718S_GPIO2_VBUS_CTRL				0xEB
#define RT1718S_GPIO1_CTRL				0xED
#define RT1718S_GPIO2_CTRL				0xEE

#define RT1718S_UNLOCK_PW_2				0xF0
#define RT1718S_UNLOCK_PW_1				0xF1

#define RT1718S_RT2_SYS_CTRL5				0xF210
#define RT1718S_RT2_VBUS_OCRC_EN			0xF214
#define RT1718S_RT2_VBUS_OCRC_EN_VBUS_OCP1_EN		BIT(0)
#define RT1718S_RT2_VBUS_OCP_CTRL1			0xF216
#define RT1718S_RT2_VBUS_OCP_CTRL4			0xF219

extern const struct tcpm_drv rt1718s_tcpm_drv;

int rt1718s_write8(int port, int reg, int val);
int rt1718s_read8(int port, int reg, int *val);
int rt1718s_update_bits8(int port, int reg, int mask, int val);
__override_proto int board_rt1718s_init(int port);

#endif /* __CROS_EC_USB_PD_TCPM_MT6370_H */
