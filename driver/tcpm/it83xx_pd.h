/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */
#ifndef __CROS_EC_DRIVER_TCPM_IT83XX_H
#define __CROS_EC_DRIVER_TCPM_IT83XX_H

/* USBPD Controller */
#if defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)
#define IT83XX_USBPD_BASE(port)   (0x00F03700 + (0x100 * (port)))

#define IT83XX_USBPD_GCR(p)       REG8(IT83XX_USBPD_BASE(p)+0x0)
#define USBPD_REG_MASK_SW_RESET_BIT               BIT(7)
#define USBPD_REG_MASK_TYPE_C_DETECT_RESET        BIT(6)
#define USBPD_REG_MASK_BMC_PHY                    BIT(4)
#define USBPD_REG_MASK_AUTO_SEND_SW_RESET         BIT(3)
#define USBPD_REG_MASK_AUTO_SEND_HW_RESET         BIT(2)
#define USBPD_REG_MASK_SNIFFER_MODE               BIT(1)
#define USBPD_REG_MASK_GLOBAL_ENABLE              BIT(0)
#define IT83XX_USBPD_PDMSR(p)     REG8(IT83XX_USBPD_BASE(p)+0x01)
#define USBPD_REG_MASK_SOPPP_ENABLE               BIT(7)
#define USBPD_REG_MASK_SOPP_ENABLE                BIT(6)
#define USBPD_REG_MASK_SOP_ENABLE                 BIT(5)
#define IT83XX_USBPD_CCGCR(p)     REG8(IT83XX_USBPD_BASE(p)+0x04)
#define USBPD_REG_MASK_DISABLE_CC                 BIT(4)
#define IT83XX_USBPD_CCCSR(p)     REG8(IT83XX_USBPD_BASE(p)+0x05)
#define USBPD_REG_MASK_CC2_DISCONNECT             BIT(7)
#define USBPD_REG_MASK_CC2_DISCONNECT_5_1K_TO_GND BIT(6)
#define USBPD_REG_MASK_CC1_DISCONNECT             BIT(3)
#define USBPD_REG_MASK_CC1_DISCONNECT_5_1K_TO_GND BIT(2)
#ifdef IT83XX_USBPD_CC_VOLTAGE_DETECTOR_INDEPENDENT
#define USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR    (BIT(5) | BIT(1))
#else
#define USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR    BIT(1)
#endif
#define IT83XX_USBPD_CCPSR(p)     REG8(IT83XX_USBPD_BASE(p)+0x06)
#define USBPD_REG_MASK_DISCONNECT_5_1K_CC2_DB     BIT(6)
#define USBPD_REG_MASK_DISCONNECT_POWER_CC2       BIT(5)
#define USBPD_REG_MASK_DISCONNECT_5_1K_CC1_DB     BIT(2)
#define USBPD_REG_MASK_DISCONNECT_POWER_CC1       BIT(1)
#define IT83XX_USBPD_DFPVDR(p)    REG8(IT83XX_USBPD_BASE(p)+0x08)
#define IT83XX_USBPD_UFPVDR(p)    REG8(IT83XX_USBPD_BASE(p)+0x09)
#define IT83XX_USBPD_PDPSR(p)     REG8(IT83XX_USBPD_BASE(p)+0x0B)
#define USBPD_REG_MASK_AUTO_FRS_DISABLE           BIT(7)
#define IT83XX_USBPD_CCADCR(p)    REG8(IT83XX_USBPD_BASE(p)+0x0C)
#define IT83XX_USBPD_ISR(p)       REG8(IT83XX_USBPD_BASE(p)+0x14)
#define USBPD_REG_MASK_TYPE_C_DETECT              BIT(7)
#define USBPD_REG_MASK_CABLE_RESET_DETECT         BIT(6)
#define USBPD_REG_MASK_HARD_RESET_DETECT          BIT(5)
#define USBPD_REG_MASK_MSG_RX_DONE                BIT(4)
#define USBPD_REG_MASK_AUTO_SOFT_RESET_TX_DONE    BIT(3)
#define USBPD_REG_MASK_HARD_RESET_TX_DONE         BIT(2)
#define USBPD_REG_MASK_MSG_TX_DONE                BIT(1)
#define USBPD_REG_MASK_TIMER_TIMEOUT              BIT(0)
#define IT83XX_USBPD_IMR(p)       REG8(IT83XX_USBPD_BASE(p)+0x15)
#define IT83XX_USBPD_MTCR(p)      REG8(IT83XX_USBPD_BASE(p)+0x18)
#define USBPD_REG_MASK_SW_RESET_TX_STAT           BIT(3)
#define USBPD_REG_MASK_TX_BUSY_STAT               BIT(2)
#define USBPD_REG_MASK_TX_DISCARD_STAT            BIT(2)
#ifdef IT83XX_PD_TX_ERROR_STATUS_BIT5
#define USBPD_REG_MASK_TX_ERR_STAT                BIT(5)
#else
#define USBPD_REG_MASK_TX_ERR_STAT                BIT(1)
#endif
#define USBPD_REG_MASK_TX_START                   BIT(0)
#define IT83XX_USBPD_MTSR0(p)     REG8(IT83XX_USBPD_BASE(p)+0x19)
#define USBPD_REG_MASK_CABLE_ENABLE               BIT(7)
#define USBPD_REG_MASK_SEND_HW_RESET              BIT(6)
#define USBPD_REG_MASK_SEND_BIST_MODE_2           BIT(5)
#define IT83XX_USBPD_MTSR1(p)     REG8(IT83XX_USBPD_BASE(p)+0x1A)
#define IT83XX_USBPD_VDMMCSR(p)   REG8(IT83XX_USBPD_BASE(p)+0x1B)
#define IT83XX_USBPD_MRSR(p)      REG8(IT83XX_USBPD_BASE(p)+0x1C)
#define USBPD_REG_GET_SOP_TYPE_RX(mrsr)           (((mrsr) >> 4) & 0x7)
#define USBPD_REG_MASK_RX_MSG_VALID               BIT(0)
#define IT83XX_USBPD_PEFSMR(p)    REG8(IT83XX_USBPD_BASE(p)+0x1D)
#define IT83XX_USBPD_PES0R(p)     REG8(IT83XX_USBPD_BASE(p)+0x1E)
#define IT83XX_USBPD_PES1R(p)     REG8(IT83XX_USBPD_BASE(p)+0x1F)
#define IT83XX_USBPD_TDO(p)       REG32(IT83XX_USBPD_BASE(p)+0x20)
#define IT83XX_USBPD_AGTMHLR(p)   REG8(IT83XX_USBPD_BASE(p)+0x3C)
#define IT83XX_USBPD_AGTMHHR(p)   REG8(IT83XX_USBPD_BASE(p)+0x3D)
#define IT83XX_USBPD_TMHLR(p)     REG8(IT83XX_USBPD_BASE(p)+0x3E)
#define IT83XX_USBPD_TMHHR(p)     REG8(IT83XX_USBPD_BASE(p)+0x3F)
#define IT83XX_USBPD_RDO0(p)      REG32(IT83XX_USBPD_BASE(p)+0x40)
#define IT83XX_USBPD_RMH(p)       REG16(IT83XX_USBPD_BASE(p)+0x5E)
#define IT83XX_USBPD_CCPSR0(p)    REG8(IT83XX_USBPD_BASE(p)+0x60)
#define IT83XX_USBPD_BMCSR(p)     REG8(IT83XX_USBPD_BASE(p)+0x64)
#define IT83XX_USBPD_PDMHSR(p)    REG8(IT83XX_USBPD_BASE(p)+0x65)
#define IT83XX_USBPD_TCDCR(p)     REG8(IT83XX_USBPD_BASE(p)+0x67)
#define USBPD_REG_PLUG_OUT_DETECT_TYPE_SELECT     BIT(7)
#define USBPD_REG_MASK_TYPEC_PLUG_IN_OUT_ISR      BIT(4)
#define USBPD_REG_PLUG_OUT_SELECT                 BIT(3)
#define USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE      BIT(1)
#define USBPD_REG_PLUG_IN_OUT_DETECT_STAT         BIT(0)
#define IT83XX_USBPD_PDQSCR(p)    REG8(IT83XX_USBPD_BASE(p)+0x70)
#define USBPD_REG_FAST_SWAP_REQUEST_ENABLE     BIT(1)
#define USBPD_REG_FAST_SWAP_DETECT_ENABLE      BIT(0)
#define IT83XX_USBPD_PD30IR(p)    REG8(IT83XX_USBPD_BASE(p)+0x78)
#define USBPD_REG_FAST_SWAP_DETECT_STAT        BIT(4)
#define IT83XX_USBPD_MPD30IR(p)   REG8(IT83XX_USBPD_BASE(p)+0x7A)
#define USBPD_REG_MASK_PD30_ISR                BIT(7)
#define USBPD_REG_MASK_FAST_SWAP_DETECT_ISR    BIT(4)

#elif defined(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2)
#define IT83XX_USBPD_BASE(port)     (0x00F03700 + (0x100 * (port) * (port)))

#define IT83XX_USBPD_PDGCR(p)       REG8(IT83XX_USBPD_BASE(p)+0x0)
#define USBPD_REG_MASK_SW_RESET_BIT               BIT(7)
#define USBPD_REG_MASK_PROTOCOL_STATE_CLEAR       BIT(6)
#define USBPD_REG_MASK_BIST_DATA_MODE             BIT(4)
#define USBPD_REG_MASK_AUTO_BIST_RESPONSE         BIT(3)
#define USBPD_REG_MASK_TX_MESSAGE_ENABLE          BIT(2)
#define USBPD_REG_MASK_SNIFFER_MODE               BIT(1)
#define USBPD_REG_MASK_BMC_PHY                    BIT(0)
#define IT83XX_USBPD_PDCSR0(p)      REG8(IT83XX_USBPD_BASE(p)+0x01)
#define IT83XX_USBPD_PDMSR(p)       REG8(IT83XX_USBPD_BASE(p)+0x02)
#define USBPD_REG_MASK_DISABLE_AUTO_GEN_TX_HEADER BIT(7)
#define USBPD_REG_MASK_AUTO_FRS_DISABLE           BIT(6)
#define IT83XX_USBPD_PDCSR1(p)      REG8(IT83XX_USBPD_BASE(p)+0x03)
#define USBPD_REG_MASK_CABLE_RESET_RX_ENABLE      BIT(6)
#define USBPD_REG_MASK_HARD_RESET_RX_ENABLE       BIT(5)
#define USBPD_REG_MASK_SOPPP_RX_ENABLE            BIT(2)
#define USBPD_REG_MASK_SOPP_RX_ENABLE             BIT(1)
#define USBPD_REG_MASK_SOP_RX_ENABLE              BIT(0)
#define IT83XX_USBPD_CCGCR(p)       REG8(IT83XX_USBPD_BASE(p)+0x04)
#define USBPD_REG_MASK_DISABLE_CC                 BIT(7)
#define USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR    BIT(6)
#define USBPD_REG_MASK_CC_SELECT_RP_RESERVED      (BIT(3) | BIT(2) | BIT(1))
#define USBPD_REG_MASK_CC_SELECT_RP_DEF           (BIT(3) | BIT(2))
#define USBPD_REG_MASK_CC_SELECT_RP_1A5           BIT(3)
#define USBPD_REG_MASK_CC_SELECT_RP_3A0           BIT(2)
#define USBPD_REG_MASK_CC1_CC2_SELECTION          BIT(0)
#define IT83XX_USBPD_CCCSR(p)       REG8(IT83XX_USBPD_BASE(p)+0x05)
#define USBPD_REG_MASK_CC2_DISCONNECT             BIT(7)
#define USBPD_REG_MASK_CC2_DISCONNECT_5_1K_TO_GND BIT(6)
#define USBPD_REG_MASK_CC1_DISCONNECT             BIT(3)
#define USBPD_REG_MASK_CC1_DISCONNECT_5_1K_TO_GND BIT(2)
#ifdef IT83XX_USBPD_CC1_CC2_RESISTANCE_SEPARATE
#define USBPD_REG_MASK_CC1_CC2_RP_RD_SELECT       (BIT(1) | BIT(5))
#else
#define USBPD_REG_MASK_CC1_CC2_RP_RD_SELECT       BIT(1)
#endif
#define IT83XX_USBPD_CCPSR(p)       REG8(IT83XX_USBPD_BASE(p)+0x06)
#define USBPD_REG_MASK_DISCONNECT_5_1K_CC2_DB     BIT(6)
#define USBPD_REG_MASK_DISCONNECT_POWER_CC2       BIT(5)
#define USBPD_REG_MASK_DISCONNECT_5_1K_CC1_DB     BIT(2)
#define USBPD_REG_MASK_DISCONNECT_POWER_CC1       BIT(1)
#define IT83XX_USBPD_SRCVCRR(p)     REG8(IT83XX_USBPD_BASE(p)+0x08)
#define USBPD_REG_MASK_SRC_COMPARE_CC2_VOLT_H     BIT(5)
#define USBPD_REG_MASK_SRC_COMPARE_CC2_VOLT_L     BIT(4)
#define USBPD_REG_MASK_SRC_COMPARE_CC1_VOLT_H     BIT(1)
#define USBPD_REG_MASK_SRC_COMPARE_CC1_VOLT_L     BIT(0)
#define IT83XX_USBPD_SNKVCRR(p)     REG8(IT83XX_USBPD_BASE(p)+0x09)
#define USBPD_REG_MASK_SNK_COMPARE_CC2_VOLT_H     BIT(6)
#define USBPD_REG_MASK_SNK_COMPARE_CC2_VOLT_M     BIT(5)
#define USBPD_REG_MASK_SNK_COMPARE_CC2_VOLT_L     BIT(4)
#define USBPD_REG_MASK_SNK_COMPARE_CC1_VOLT_H     BIT(2)
#define USBPD_REG_MASK_SNK_COMPARE_CC1_VOLT_M     BIT(1)
#define USBPD_REG_MASK_SNK_COMPARE_CC1_VOLT_L     BIT(0)
#define IT83XX_USBPD_PDFSCR(p)      REG8(IT83XX_USBPD_BASE(p)+0x0C)
#define USBPD_REG_FAST_SWAP_REQUEST_ENABLE        BIT(1)
#define USBPD_REG_FAST_SWAP_DETECT_ENABLE         BIT(0)
#define IT83XX_USBPD_IFS(p)         REG8(IT83XX_USBPD_BASE(p)+0x12)
#define USBPD_REG_FAST_SWAP_DETECT_STAT           BIT(4)
#define IT83XX_USBPD_MIFS(p)        REG8(IT83XX_USBPD_BASE(p)+0x13)
#define USBPD_REG_MASK_FAST_SWAP_ISR              BIT(7)
#define USBPD_REG_MASK_FAST_SWAP_DETECT_ISR       BIT(4)
#define IT83XX_USBPD_ISR(p)         REG8(IT83XX_USBPD_BASE(p)+0x14)
#define USBPD_REG_MASK_CABLE_RESET_DETECT         BIT(6)
#define USBPD_REG_MASK_HARD_RESET_DETECT          BIT(5)
#define USBPD_REG_MASK_MSG_RX_DONE                BIT(4)
#define USBPD_REG_MASK_TX_ERROR_STAT              BIT(3)
#define USBPD_REG_MASK_CABLE_RESET_TX_DONE        BIT(2)
#define USBPD_REG_MASK_HARD_RESET_TX_DONE         BIT(1)
#define USBPD_REG_MASK_MSG_TX_DONE                BIT(0)
#define IT83XX_USBPD_IMR(p)         REG8(IT83XX_USBPD_BASE(p)+0x15)
#define IT83XX_USBPD_MTCR(p)        REG8(IT83XX_USBPD_BASE(p)+0x18)
#define USBPD_REG_MASK_TX_DISCARD_STAT            BIT(7)
#define USBPD_REG_MASK_TX_NO_RESPONSE_STAT        BIT(6)
#define USBPD_REG_MASK_TX_NOT_EN_STAT             BIT(5)
#define USBPD_REG_MASK_CABLE_RESET                BIT(3)
#define USBPD_REG_MASK_SEND_HW_RESET              BIT(2)
#define USBPD_REG_MASK_SEND_BIST_MODE_2           BIT(1)
#define USBPD_REG_MASK_TX_START                   BIT(0)
#define IT83XX_USBPD_MTSR0(p)       REG8(IT83XX_USBPD_BASE(p)+0x19)
#define IT83XX_USBPD_MHSR0(p)       REG8(IT83XX_USBPD_BASE(p)+0x1A)
#define USBPD_REG_MASK_SOP_PORT_DATA_ROLE         BIT(5)
#define IT83XX_USBPD_MHSR1(p)       REG8(IT83XX_USBPD_BASE(p)+0x1B)
#define USBPD_REG_MASK_SOP_PORT_POWER_ROLE        BIT(0)
#define IT83XX_USBPD_TDO(p)         REG32(IT83XX_USBPD_BASE(p)+0x22)
#define IT83XX_USBPD_RMH(p)         REG16(IT83XX_USBPD_BASE(p)+0x42)
#define IT83XX_USBPD_RDO(p)         REG32(IT83XX_USBPD_BASE(p)+0x44)
#define IT83XX_USBPD_BMCDR0(p)      REG8(IT83XX_USBPD_BASE(p)+0x61)
#define USBPD_REG_MASK_BMC_RX_THRESHOLD_SRC       BIT(5)
#define USBPD_REG_MASK_BMC_RX_THRESHOLD_SNK       BIT(1)
#define IT83XX_USBPD_TCDCR(p)       REG8(IT83XX_USBPD_BASE(p)+0x67)
#define USBPD_REG_PLUG_OUT_DETECT_TYPE_SELECT     BIT(7)
#define USBPD_REG_PLUG_OUT_SELECT                 BIT(6)
#define USBPD_REG_PD3_0_SNK_TX_OK_DISABLE         BIT(5)
#define USBPD_REG_PD3_0_SNK_TX_NG_DISABLE         BIT(3)
#define USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE      BIT(1)
#define USBPD_REG_PLUG_IN_OUT_DETECT_STAT         BIT(0)
#define IT83XX_USBPD_CCPSR0(p)      REG8(IT83XX_USBPD_BASE(p)+0x70)
#endif /* !defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX) */

/*
 * Dedicated setting for CC pin.
 * This setting will connect CC pin to internal PD module directly without
 * applying any GPIO/ALT configuration.
 */
#define IT83XX_USBPD_CC_PIN_CONFIG  0x86
#define IT83XX_USBPD_CC_PIN_CONFIG2 0x06

#ifndef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT   0
#endif

#define TASK_EVENT_PHY_TX_DONE TASK_EVENT_CUSTOM_BIT(PD_EVENT_FIRST_FREE_BIT)

#define SET_MASK(reg, bit_mask)      ((reg) |= (bit_mask))
#define CLEAR_MASK(reg, bit_mask)    ((reg) &= (~(bit_mask)))
#define IS_MASK_SET(reg, bit_mask)   (((reg) & (bit_mask)) != 0)
#define IS_MASK_CLEAR(reg, bit_mask) (((reg) & (bit_mask)) == 0)

#if defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)
/* macros for set */
#define USBPD_KICK_TX_START(port)            \
	SET_MASK(IT83XX_USBPD_MTCR(port),    \
		USBPD_REG_MASK_TX_START)
#define USBPD_SEND_HARD_RESET(port)          \
	SET_MASK(IT83XX_USBPD_MTSR0(port),   \
		USBPD_REG_MASK_SEND_HW_RESET)
#define USBPD_SW_RESET(port)                 \
	SET_MASK(IT83XX_USBPD_GCR(port),     \
		USBPD_REG_MASK_SW_RESET_BIT)
#define USBPD_ENABLE_BMC_PHY(port)           \
	SET_MASK(IT83XX_USBPD_GCR(port),     \
		USBPD_REG_MASK_BMC_PHY)
#define USBPD_DISABLE_BMC_PHY(port)          \
	CLEAR_MASK(IT83XX_USBPD_GCR(port),   \
		USBPD_REG_MASK_BMC_PHY)
#define USBPD_START(port)                    \
	CLEAR_MASK(IT83XX_USBPD_CCGCR(port), \
		USBPD_REG_MASK_DISABLE_CC)
#define USBPD_ENABLE_SEND_BIST_MODE_2(port)  \
	SET_MASK(IT83XX_USBPD_MTSR0(port),   \
		USBPD_REG_MASK_SEND_BIST_MODE_2)
#define USBPD_DISABLE_SEND_BIST_MODE_2(port) \
	CLEAR_MASK(IT83XX_USBPD_MTSR0(port), \
		USBPD_REG_MASK_SEND_BIST_MODE_2)
#define USBPD_CLEAR_FRS_DETECT_STATUS(port)  \
	(IT83XX_USBPD_PD30IR(port) = USBPD_REG_FAST_SWAP_DETECT_STAT)
#define USBPD_CC1_DISCONNECTED(p) \
	((IT83XX_USBPD_CCCSR(p) | USBPD_REG_MASK_CC1_DISCONNECT) & \
	~USBPD_REG_MASK_CC2_DISCONNECT)
#define USBPD_CC2_DISCONNECTED(p) \
	((IT83XX_USBPD_CCCSR(p) | USBPD_REG_MASK_CC2_DISCONNECT) & \
	~USBPD_REG_MASK_CC1_DISCONNECT)

/* macros for get */
#define USBPD_GET_POWER_ROLE(port)                  \
	(IT83XX_USBPD_PDMSR(port) & 1)
#define USBPD_GET_CC1_PULL_REGISTER_SELECTION(port) \
	(IT83XX_USBPD_CCGCR(port) & BIT(1))
#define USBPD_GET_CC2_PULL_REGISTER_SELECTION(port) \
	(IT83XX_USBPD_BMCSR(port) & BIT(3))
#define USBPD_GET_PULL_CC_SELECTION(port)           \
	(IT83XX_USBPD_CCGCR(port) & 1)

/* macros for check */
#define USBPD_IS_TX_ERR(port)       \
	IS_MASK_SET(IT83XX_USBPD_MTCR(port), USBPD_REG_MASK_TX_ERR_STAT)
#define USBPD_IS_TX_DISCARD(port)   \
	IS_MASK_SET(IT83XX_USBPD_MTCR(port), USBPD_REG_MASK_TX_DISCARD_STAT)

#elif defined(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2)
/* macros for set */
#define USBPD_SW_RESET(port)                 \
	SET_MASK(IT83XX_USBPD_PDGCR(port),   \
		USBPD_REG_MASK_SW_RESET_BIT)
#define USBPD_ENABLE_BMC_PHY(port)           \
	SET_MASK(IT83XX_USBPD_PDGCR(port),   \
		USBPD_REG_MASK_BMC_PHY)
#define USBPD_DISABLE_BMC_PHY(port)          \
	CLEAR_MASK(IT83XX_USBPD_PDGCR(port), \
		USBPD_REG_MASK_BMC_PHY)
#define USBPD_START(port)                    \
	CLEAR_MASK(IT83XX_USBPD_CCGCR(port), \
		USBPD_REG_MASK_DISABLE_CC)
#define USBPD_SEND_HARD_RESET(port)          \
	SET_MASK(IT83XX_USBPD_MTCR(port),    \
		USBPD_REG_MASK_SEND_HW_RESET)
#define USBPD_SEND_CABLE_RESET(port)         \
	SET_MASK(IT83XX_USBPD_MTCR(port),    \
		USBPD_REG_MASK_CABLE_RESET)
#define USBPD_ENABLE_SEND_BIST_MODE_2(port)  \
	SET_MASK(IT83XX_USBPD_MTCR(port),    \
		USBPD_REG_MASK_SEND_BIST_MODE_2)
#define USBPD_DISABLE_SEND_BIST_MODE_2(port) \
	CLEAR_MASK(IT83XX_USBPD_MTCR(port),  \
		USBPD_REG_MASK_SEND_BIST_MODE_2)
#define USBPD_KICK_TX_START(port)            \
	SET_MASK(IT83XX_USBPD_MTCR(port),    \
		USBPD_REG_MASK_TX_START)
#define USBPD_CLEAR_FRS_DETECT_STATUS(port)  \
	(IT83XX_USBPD_IFS(port) = USBPD_REG_FAST_SWAP_DETECT_STAT)
#define USBPD_CC1_DISCONNECTED(p) \
	((IT83XX_USBPD_CCCSR(p) | USBPD_REG_MASK_CC1_DISCONNECT) & \
	~USBPD_REG_MASK_CC2_DISCONNECT)
#define USBPD_CC2_DISCONNECTED(p) \
	((IT83XX_USBPD_CCCSR(p) | USBPD_REG_MASK_CC2_DISCONNECT) & \
	~USBPD_REG_MASK_CC1_DISCONNECT)

/* macros for get */
#define USBPD_GET_POWER_ROLE(port)                  \
	(IT83XX_USBPD_MHSR1(port) & BIT(0))
#define USBPD_GET_CC1_PULL_REGISTER_SELECTION(port) \
	(IT83XX_USBPD_CCCSR(port) & BIT(1))
#define USBPD_GET_CC2_PULL_REGISTER_SELECTION(port) \
	(IT83XX_USBPD_CCCSR(port) & BIT(1))
#define USBPD_GET_PULL_CC_SELECTION(port)           \
	(IT83XX_USBPD_CCGCR(port) & BIT(0))
#define USBPD_GET_SNK_COMPARE_CC1_VOLT(port)        \
	(IT83XX_USBPD_SNKVCRR(port) &               \
		(USBPD_REG_MASK_SNK_COMPARE_CC1_VOLT_L |  \
		USBPD_REG_MASK_SNK_COMPARE_CC1_VOLT_M |   \
		USBPD_REG_MASK_SNK_COMPARE_CC1_VOLT_H))
#define USBPD_GET_SNK_COMPARE_CC2_VOLT(port)        \
	((IT83XX_USBPD_SNKVCRR(port) &              \
		(USBPD_REG_MASK_SNK_COMPARE_CC2_VOLT_L |  \
		USBPD_REG_MASK_SNK_COMPARE_CC2_VOLT_M |   \
		USBPD_REG_MASK_SNK_COMPARE_CC2_VOLT_H)) >> 4)
#define USBPD_GET_SRC_COMPARE_CC1_VOLT(port)        \
	(IT83XX_USBPD_SRCVCRR(port) &               \
		(USBPD_REG_MASK_SRC_COMPARE_CC1_VOLT_L |  \
		USBPD_REG_MASK_SRC_COMPARE_CC1_VOLT_H))
#define USBPD_GET_SRC_COMPARE_CC2_VOLT(port)        \
	((IT83XX_USBPD_SRCVCRR(port) &              \
		(USBPD_REG_MASK_SRC_COMPARE_CC2_VOLT_L |  \
		USBPD_REG_MASK_SRC_COMPARE_CC2_VOLT_H)) >> 4)

/* macros for check */
#define USBPD_IS_TX_ERR(port)           \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_TX_ERROR_STAT)
#endif /* !defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX) */

/* macros for PD ISR */
#define USBPD_IS_HARD_RESET_DETECT(port) \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_HARD_RESET_DETECT)
#define USBPD_IS_TX_DONE(port)           \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_MSG_TX_DONE)
#define USBPD_IS_RX_DONE(port)           \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_MSG_RX_DONE)
#define USBPD_IS_PLUG_IN_OUT_DETECT(port)\
	IS_MASK_SET(IT83XX_USBPD_TCDCR(port), USBPD_REG_PLUG_IN_OUT_DETECT_STAT)
#define USBPD_IS_PLUG_IN(port)           \
	IS_MASK_CLEAR(IT83XX_USBPD_TCDCR(port), USBPD_REG_PLUG_OUT_SELECT)
#if defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)
#define USBPD_IS_FAST_SWAP_DETECT(port)  \
	IS_MASK_SET(IT83XX_USBPD_PD30IR(port), USBPD_REG_FAST_SWAP_DETECT_STAT)
#elif defined(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2)
#define USBPD_IS_FAST_SWAP_DETECT(port)  \
	IS_MASK_SET(IT83XX_USBPD_IFS(port), USBPD_REG_FAST_SWAP_DETECT_STAT)
#endif

enum usbpd_port {
	USBPD_PORT_A,
	USBPD_PORT_B,
	USBPD_PORT_C,
};

enum usbpd_ufp_volt_status {
	USBPD_UFP_STATE_SNK_OPEN = 0,
	USBPD_UFP_STATE_SNK_DEF  = 1,
	USBPD_UFP_STATE_SNK_1_5  = 3,
	USBPD_UFP_STATE_SNK_3_0  = 7,
};

enum usbpd_dfp_volt_status {
	USBPD_DFP_STATE_SRC_RA   = 0,
	USBPD_DFP_STATE_SRC_RD   = 1,
	USBPD_DFP_STATE_SRC_OPEN = 3,
};

enum usbpd_power_role {
	USBPD_POWER_ROLE_CONSUMER,
	USBPD_POWER_ROLE_PROVIDER,
	USBPD_POWER_ROLE_CONSUMER_PROVIDER,
	USBPD_POWER_ROLE_PROVIDER_CONSUMER,
};

struct usbpd_ctrl_t {
	volatile uint8_t *cc1;
	volatile uint8_t *cc2;
	uint8_t irq;
};

extern const struct usbpd_ctrl_t usbpd_ctrl_regs[];
extern const struct tcpm_drv it83xx_tcpm_drv;
void it83xx_Rd_5_1K_only_for_hibernate(int port);
#ifdef CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2
void it83xx_clear_tx_error_status(enum usbpd_port port);
void it83xx_get_tx_error_status(enum usbpd_port port);
#endif
void switch_plug_out_type(enum usbpd_port port);

#endif /* __CROS_EC_DRIVER_TCPM_IT83XX_H */
