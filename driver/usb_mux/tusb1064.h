/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TUSB1064_H
#define __CROS_EC_TUSB1064_H

#include <stdint.h>
#include "usb_mux.h"

/*
 * TUSB1064 Has 16 possible device addresses which are selected by A1|A0 lines
 * using 4 level inputs.
 *    0 -> tied directly to GND
 *    R -> tied to GND via a 20k pulldown
 *    F -> floating
 *    1 -> tied to VCC
 */
#define TUSB1064_I2C_ADDR0_FLAGS     0x44
#define TUSB1064_I2C_ADDR1_FLAGS     0x45
#define TUSB1064_I2C_ADDR2_FLAGS     0x46
#define TUSB1064_I2C_ADDR3_FLAGS     0x47
#define TUSB1064_I2C_ADDR4_FLAGS     0x20
#define TUSB1064_I2C_ADDR5_FLAGS     0x21
#define TUSB1064_I2C_ADDR6_FLAGS     0x22
#define TUSB1064_I2C_ADDR7_FLAGS     0x23
#define TUSB1064_I2C_ADDR8_FLAGS     0x10
#define TUSB1064_I2C_ADDR9_FLAGS     0x11
#define TUSB1064_I2C_ADDR10_FLAGS    0x12
#define TUSB1064_I2C_ADDR11_FLAGS    0x13
#define TUSB1064_I2C_ADDR12_FLAGS    0x0C
#define TUSB1064_I2C_ADDR13_FLAGS    0x0D
#define TUSB1064_I2C_ADDR14_FLAGS    0x0E
#define TUSB1064_I2C_ADDR15_FLAGS    0x0F

/* TUSB1064 General Register */
#define TUSB1064_REG_GENERAL            0x0a
#define REG_GENERAL_CTLSEL_USB3         BIT(0)
#define REG_GENERAL_CTLSEL_ANYDP        BIT(1)
#define REG_GENERAL_FLIPSEL             BIT(2)
#if defined(CONFIG_USB_MUX_TUSB1044)
#define REG_GENERAL_HPDIN_OVERRIDE      BIT(3)
#else
#define REG_GENERAL_DP_EN_CTRL          BIT(3)
#endif
#define REG_GENERAL_EQ_OVERRIDE         BIT(4)

/* AUX and DP Lane Control Register */
#define TUSB1064_REG_AUXDPCTRL          0x13
#define TUSB1064_AUXDPCTRL_AUX_SNOOP_DISABLE BIT(7)
#define TUSB1064_AUXDPCTRL_AUX_SBU_OVR  0x30
#define TUSB1064_AUXDPCTRL_DP3_DISABLE  BIT(3)
#define TUSB1064_AUXDPCTRL_DP2_DISABLE  BIT(2)
#define TUSB1064_AUXDPCTRL_DP1_DISABLE  BIT(1)
#define TUSB1064_AUXDPCTRL_DP0_DISABLE  BIT(0)

/* Receiver Equalization GPIO Control */
#define TUSB1064_REG_DP1DP3EQ_SEL 0x10
#define TUSB1064_REG_DP0DP2EQ_SEL 0x11

/* DP Receiver equalization settings */
#define TUSB1064_DP_EQ_RX_NEG_0_3_DB 0x0
#define TUSB1064_DP_EQ_RX_1_6_DB 0x1
#define TUSB1064_DP_EQ_RX_3_0_DB 0x2
#define TUSB1064_DP_EQ_RX_4_4_DB 0x3
#define TUSB1064_DP_EQ_RX_5_4_DB 0x4
#define TUSB1064_DP_EQ_RX_6_5_DB 0x5
#define TUSB1064_DP_EQ_RX_7_3_DB 0x6
#define TUSB1064_DP_EQ_RX_8_1_DB 0x7
#define TUSB1064_DP_EQ_RX_8_9_DB 0x8
#define TUSB1064_DP_EQ_RX_9_5_DB 0x9
#define TUSB1064_DP_EQ_RX_10_0_DB 0xA
#define TUSB1064_DP_EQ_RX_10_6_DB 0xB
#define TUSB1064_DP_EQ_RX_11_0_DB 0xC
#define TUSB1064_DP_EQ_RX_11_4_DB 0xD
#define TUSB1064_DP_EQ_RX_11_8_DB 0xE
#define TUSB1064_DP_EQ_RX_12_1_DB 0xF

#ifndef TUSB1064_DP1EQ
#define TUSB1064_DP1EQ(nr)         ((nr) << 4)
#endif
#ifndef TUSB1064_DP3EQ
#define TUSB1064_DP3EQ(nr)         ((nr) << 0)
#endif
#ifndef TUSB1064_DP0EQ
#define TUSB1064_DP0EQ(nr)         ((nr) << 4)
#endif
#ifndef TUSB1064_DP2EQ
#define TUSB1064_DP2EQ(nr)         ((nr) << 0)
#endif


/* TUSB1064 Receiver Equalization GPIO Control */
#define TUSB1064_REG_SSRX2RX1EQ_SEL 0x20
#define TUSB1064_REG_SSTXEQ_SEL 0x21

/* USB equalization settings for Mux DFP (TX) */
#define TUSB1064_USB_EQ_DFP_NEG_3_0_DB 0x0
#define TUSB1064_USB_EQ_DFP_NEG_0_8_DB 0x1
#define TUSB1064_USB_EQ_DFP_NEG_0_7_DB 0x2
#define TUSB1064_USB_EQ_DFP_2_2_DB 0x3
#define TUSB1064_USB_EQ_DFP_3_3_DB 0x4
#define TUSB1064_USB_EQ_DFP_4_3_DB 0x5
#define TUSB1064_USB_EQ_DFP_5_1_DB 0x6
#define TUSB1064_USB_EQ_DFP_6_0_DB 0x7
#define TUSB1064_USB_EQ_DFP_6_7_DB 0x8
#define TUSB1064_USB_EQ_DFP_7_3_DB 0x9
#define TUSB1064_USB_EQ_DFP_7_8_DB 0xA
#define TUSB1064_USB_EQ_DFP_8_3_DB 0xB
#define TUSB1064_USB_EQ_DFP_8_6_DB 0xC
#define TUSB1064_USB_EQ_DFP_9_0_DB 0xD
#define TUSB1064_USB_EQ_DFP_9_3_DB 0xE
#define TUSB1064_USB_EQ_DFP_9_7_DB 0xF

/* USB equalization settings for Mux UFP (RX) */
#define TUSB1064_USB_EQ_UFP_NEG_1_5_DB 0x0
#define TUSB1064_USB_EQ_UFP_0_7_DB 0x1
#define TUSB1064_USB_EQ_UFP_2_2_DB 0x2
#define TUSB1064_USB_EQ_UFP_3_7_DB 0x3
#define TUSB1064_USB_EQ_UFP_4_7_DB 0x4
#define TUSB1064_USB_EQ_UFP_5_8_DB 0x5
#define TUSB1064_USB_EQ_UFP_6_6_DB 0x6
#define TUSB1064_USB_EQ_UFP_7_4_DB 0x7
#define TUSB1064_USB_EQ_UFP_8_1_DB 0x8
#define TUSB1064_USB_EQ_UFP_8_7_DB 0x9
#define TUSB1064_USB_EQ_UFP_9_2_DB 0xA
#define TUSB1064_USB_EQ_UFP_9_7_DB 0xB
#define TUSB1064_USB_EQ_UFP_10_0_DB 0xC
#define TUSB1064_USB_EQ_UFP_10_4_DB 0xD
#define TUSB1064_USB_EQ_UFP_10_7_DB 0xE
#define TUSB1064_USB_EQ_UFP_11_1_DB 0xF

#if defined(CONFIG_USB_MUX_TUSB1044)
/*
 * This api is used to override the HPD infomartion received on HPD_IN pin
 * or when no HPD physical pin is connected.
 * Writes HPD infomration to the General_1 Registor.
 */
void tusb1044_hpd_update(const struct usb_mux *me, mux_state_t mux_state);
#endif

#endif /* __CROS_EC_TUSB1064_H */
