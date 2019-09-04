/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MT6370 TCPC Driver
 */

#ifndef __CROS_EC_USB_PD_TCPM_MT6370_H
#define __CROS_EC_USB_PD_TCPM_MT6370_H

/* MT6370 Private RegMap */

#define MT6370_REG_PHY_CTRL1			0x80
#define MT6370_REG_PHY_CTRL2			0x81
#define MT6370_REG_PHY_CTRL3			0x82
#define MT6370_REG_PHY_CTRL6			0x85

#define MT6370_REG_CLK_CTRL2			0x87
#define MT6370_REG_CLK_CTRL3			0x88

#define MT6370_REG_RUST_STATUS			0x8A
#define MT6370_REG_RUST_INT_EVENT		0x8B
#define MT6370_REG_RUST_MASK			0x8C
#define MT6370_REG_BMC_CTRL			0x90
#define MT6370_REG_BMCIO_RXDZSEL		0x93
#define MT6370_REG_VCONN_CLIMITEN		0x95

#define MT6370_REG_OVP_FLAG_SEL			0x96

#define MT6370_REG_RT_STATUS			0x97
#define MT6370_REG_RT_INT			0x98
#define MT6370_REG_RT_MASK			0x99
#define RT5081_REG_BMCIO_RXDZEN			0x9A
#define MT6370_REG_IDLE_CTRL			0x9B
#define MT6370_REG_INTRST_CTRL			0x9C
#define MT6370_REG_WATCHDOG_CTRL		0x9D
#define MT6370_REG_I2CRST_CTRL			0X9E

#define MT6370_REG_SWRESET			0xA0
#define MT6370_REG_TTCPC_FILTER			0xA1
#define MT6370_REG_DRP_TOGGLE_CYCLE		0xA2
#define MT6370_REG_DRP_DUTY_CTRL		0xA3
#define MT6370_REG_RUST_DETECTION		0xAD
#define MT6370_REG_RUST_CONTROL			0xAE
#define MT6370_REG_BMCIO_RXDZEN			0xAF
#define MT6370_REG_DRP_RUST			0xB9

#define MT6370_REG_UNLOCK_PW2			0xF0
#define MT6370_REG_UNLOCK_PW1			0xF1

#define MT6370_TCPC_I2C_ADDR_FLAGS		0x4E

/*
 * MT6370_REG_PHY_CTRL1				0x80
 */

#define MT6370_REG_PHY_CTRL1_SET(retry_discard, toggle_cnt, bus_idle_cnt,      \
				 rx_filter)                                    \
	((retry_discard << 7) | (toggle_cnt << 4) | (bus_idle_cnt << 2) |      \
	 (rx_filter & 0x03))

/*
 * MT6370_REG_CLK_CTRL2				0x87
 */

#define MT6370_REG_CLK_DIV_600K_EN		BIT(7)
#define MT6370_REG_CLK_BCLK2_EN			BIT(6)
#define MT6370_REG_CLK_BCLK2_TG_EN		BIT(5)
#define MT6370_REG_CLK_DIV_300K_EN		BIT(3)
#define MT6370_REG_CLK_CK_300K_EN		BIT(2)
#define MT6370_REG_CLK_BCLK_EN			BIT(1)
#define MT6370_REG_CLK_BCLK_TH_EN		BIT(0)

/*
 * MT6370_REG_CLK_CTRL3				0x88
 */

#define MT6370_REG_CLK_OSCMUX_RG_EN		BIT(7)
#define MT6370_REG_CLK_CK_24M_EN		BIT(6)
#define MT6370_REG_CLK_OSC_RG_EN		BIT(5)
#define MT6370_REG_CLK_DIV_2P4M_EN		BIT(4)
#define MT6370_REG_CLK_CK_2P4M_EN		BIT(3)
#define MT6370_REG_CLK_PCLK_EN			BIT(2)
#define MT6370_REG_CLK_PCLK_RG_EN		BIT(1)
#define MT6370_REG_CLK_PCLK_TG_EN		BIT(0)

/*
 * MT6370_REG_RX_TX_DBG				0x8b
 */

#define MT6370_REG_RX_TX_DBG_RX_BUSY		BIT(7)
#define MT6370_REG_RX_TX_DBG_TX_BUSY		BIT(6)

/*
 * MT6370_REG_BMC_CTRL				0x90
 */

#define MT6370_REG_IDLE_EN			BIT(6)
#define MT6370_REG_DISCHARGE_EN			BIT(5)
#define MT6370_REG_BMCIO_LPRPRD			BIT(4)
#define MT6370_REG_BMCIO_LPEN			BIT(3)
#define MT6370_REG_BMCIO_BG_EN			BIT(2)
#define MT6370_REG_VBUS_DET_EN			BIT(1)
#define MT6370_REG_BMCIO_OSC_EN			BIT(0)
#define MT6370_REG_BMC_CTRL_DEFAULT                                            \
	(MT6370_REG_BMCIO_BG_EN | MT6370_REG_VBUS_DET_EN |                     \
	 MT6370_REG_BMCIO_OSC_EN)

/*
 * MT6370_REG_BMCIO_RXDZSEL			0x93
 */

#define MT6370_MASK_OCCTRL_SEL			0xE0
#define MT6370_OCCTRL_600MA			0x80
#define MT6370_MASK_BMCIO_RXDZSEL		BIT(0)

/*
 * MT6370_REG_OVP_FLAG_SEL			0x96
 */

#define MT6370_MASK_DISCHARGE_LVL		0x03
#define MT6370_REG_DISCHARGE_LVL		BIT(0)

/*
 * MT6370_REG_RT_STATUS				0x97
 */

#define MT6370_REG_RA_DETACH			BIT(5)
#define MT6370_REG_VBUS_80			BIT(1)

/*
 * MT6370_REG_RT_INT				0x98
 */

#define MT6370_REG_INT_RA_DETACH		BIT(5)
#define MT6370_REG_INT_WATCHDOG			BIT(2)
#define MT6370_REG_INT_VBUS_80			BIT(1)
#define MT6370_REG_INT_WAKEUP			BIT(0)

/*
 * MT6370_REG_RT_MASK				0x99
 */

#define MT6370_REG_M_RA_DETACH			BIT(5)
#define MT6370_REG_M_WATCHDOG			BIT(2)
#define MT6370_REG_M_VBUS_80			BIT(1)
#define MT6370_REG_M_WAKEUP			BIT(0)

/*
 * MT6370_REG_IDLE_CTRL				0x9B
 */

#define MT6370_REG_CK_300K_SEL			BIT(7)
#define MT6370_REG_SHIPPING_OFF			BIT(5)
#define MT6370_REG_ENEXTMSG			BIT(4)
#define MT6370_REG_AUTOIDLE_EN			BIT(3)

/* timeout = (tout*2+1) * 6.4ms */
#ifdef CONFIG_USB_PD_REV30
#define MT6370_REG_IDLE_SET(ck300, ship_dis, auto_idle, tout)                  \
	((ck300 << 7) | (ship_dis << 5) | (auto_idle << 3) | (tout & 0x07) |   \
	 MT6370_REG_ENEXTMSG)
#else
#define MT6370_REG_IDLE_SET(ck300, ship_dis, auto_idle, tout)                  \
	((ck300 << 7) | (ship_dis << 5) | (auto_idle << 3) | (tout & 0x07))
#endif

/*
 * MT6370_REG_INTRST_CTRL			0x9C
 */

#define MT6370_REG_INTRST_EN			BIT(7)

/* timeout = (tout+1) * 0.2sec */
#define MT6370_REG_INTRST_SET(en, tout)		((en << 7) | (tout & 0x03))

/*
 * MT6370_REG_WATCHDOG_CTRL			0x9D
 */

#define MT6370_REG_WATCHDOG_EN			BIT(7)

/* timeout = (tout+1) * 0.4sec */
#define MT6370_REG_WATCHDOG_CTRL_SET(en, tout)	((en << 7) | (tout & 0x07))

/*
 * MT6370_REG_I2CRST_CTRL			0x9E
 */

#define MT6370_REG_I2CRST_EN			BIT(7)

/* timeout = (tout+1) * 12.5ms */
#define MT6370_REG_I2CRST_SET(en, tout)		((en << 7) | (tout & 0x0f))

extern const struct tcpm_drv mt6370_tcpm_drv;

/* Enable VCONN discharge. */
int mt6370_vconn_discharge(int port);

#endif /* __CROS_EC_USB_PD_TCPM_MT6370_H */
