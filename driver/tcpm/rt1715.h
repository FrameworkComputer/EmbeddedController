/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Richtek RT1715 Type-C port controller */
#ifndef __CROS_EC_USB_PD_TCPM_RT1715_H
#define __CROS_EC_USB_PD_TCPM_RT1715_H

/* I2C interface */
#define RT1715_I2C_ADDR_FLAGS			0x4E

#define RT1715_VENDOR_ID			0x29CF

#define RT1715_REG_VENDOR_7			0xA0
#define RT1715_REG_VENDOR_7_SOFT_RESET		BIT(0)

#define RT1715_REG_PHY_CTRL1			0x80
/* Wait for tReceive before retrying transmit in response to a bad GoodCRC */
#define RT1715_REG_PHY_CTRL1_ENRETRY		BIT(7)
/*
 * Bit 6:4 <TRANSCNT>: Consider CC to be idle if there are 7 or fewer BMC
 * transients observed in <46.67us>
 */
#define RT1715_REG_PHY_CTRL1_TRANSCNT_7		0x70
/*
 * Bit 1:0 <TRXFilter>: RX filter to make sure the stable received PD message.
 * default value is 01b
 * The debounce time is (register value + 2) * 41.67ns
 */
#define RT1715_REG_PHY_CTRL1_TRXFILTER_125NS	0x01
#define RT1715_REG_PHY_CTRL2			0x81
/*
 * Decrease the time that the PHY will wait for a second transition to detect
 * a BMC-encoded 1 bit from 2.67 us to 2.25 us.
 * Timeout = register value * .04167 us.
 */
#define RT1715_REG_PHY_CTRL2_CDRTHRESH_2_25US	54
#define RT1715_REG_PHY_CTRL2_CDRTHRESH_2_5US	60
#define RT1715_REG_PHY_CTRL2_CDRTHRESH_2_58US	62

#define RT1715_REG_PWR				0x90
#define RT1715_REG_PWR_BMCIO_LPEN		BIT(3)
#define RT1715_REG_PWR_VBUS_DETEN		BIT(1)
#define RT1715_REG_PWR_BMCIO_OSCEN		BIT(0)

#define RT1715_REG_BMCIO_RXDZSEL		0x93
#define RT1715_REG_BMCIO_RXDZSEL_OCCTRL_600MA	BIT(7)
#define RT1715_REG_BMCIO_RXDZSEL_SEL		BIT(0)

#define RT1715_REG_RT_INT			0x98
#define RT1715_REG_RT_INT_WAKEUP		BIT(0)

#define RT1715_REG_RT_MASK			0x99
#define RT1715_REG_RT_MASK_M_WAKEUP		BIT(0)

#define RT1715_REG_VENDOR_5			0x9B
#define RT1715_REG_VENDOR_5_SHUTDOWN_OFF	BIT(5)
#define RT1715_REG_VENDOR_5_ENEXTMSG		BIT(4)
#define RT1715_REG_VENDOR_5_AUTOIDLE_EN		BIT(3)

#define RT1715_REG_I2CRST_CTRL			0x9E
/* I2C reset : (val + 1) * 12.5ms */
#define RT1715_REG_I2CRST_CTRL_TOUT_200MS	0x0F
#define RT1715_REG_I2CRST_CTRL_TOUT_150MS	0x0B
#define RT1715_REG_I2CRST_CTRL_TOUT_100MS	0x07
#define RT1715_REG_I2CRST_CTRL_EN		BIT(7)


#define RT1715_REG_TTCPC_FILTER			0xA1
#define RT1715_REG_TTCPC_FILTER_400US		0x0F

#define RT1715_REG_DRP_TOGGLE_CYCLE		0xA2
/* DRP Duty : (51.2 + 6.4 * val) ms */
#define RT1715_REG_DRP_TOGGLE_CYCLE_76MS	0x04

#define RT1715_REG_DRP_DUTY_CTRL		0xA3
#define RT1715_REG_DRP_DUTY_CTRL_40PERCENT	400

#define RT1715_REG_BMCIO_RXDZEN			0xAF
#define RT1715_REG_BMCIO_RXDZEN_ENABLE		0x01
#define RT1715_REG_BMCIO_RXDZEN_DISABLE		0x00

extern const struct tcpm_drv rt1715_tcpm_drv;

#endif /* defined(__CROS_EC_USB_PD_TCPM_RT1715_H) */
