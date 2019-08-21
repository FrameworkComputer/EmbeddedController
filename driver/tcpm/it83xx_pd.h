/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */
#ifndef __CROS_EC_DRIVER_TCPM_IT83XX_H
#define __CROS_EC_DRIVER_TCPM_IT83XX_H

/*
 * Dedicated setting for CC pin.
 * This setting will connect CC pin to internal PD module directly without
 * applying any GPIO/ALT configuration.
 */
#define IT83XX_USBPD_CC_PIN_CONFIG 0x86

#define TASK_EVENT_PHY_TX_DONE TASK_EVENT_CUSTOM_BIT(PD_EVENT_FIRST_FREE_BIT)

#define SET_MASK(reg, bit_mask)      ((reg) |= (bit_mask))
#define CLEAR_MASK(reg, bit_mask)    ((reg) &= (~(bit_mask)))
#define IS_MASK_SET(reg, bit_mask)   (((reg) & (bit_mask)) != 0)
#define IS_MASK_CLEAR(reg, bit_mask) (((reg) & (bit_mask)) == 0)

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

/* macros for PD ISR */
#define USBPD_IS_HARD_RESET_DETECT(port) \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_HARD_RESET_DETECT)
#define USBPD_IS_TX_DONE(port)           \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_MSG_TX_DONE)
#define USBPD_IS_RX_DONE(port)           \
	IS_MASK_SET(IT83XX_USBPD_ISR(port), USBPD_REG_MASK_MSG_RX_DONE)
#ifdef IT83XX_INTC_PLUG_IN_SUPPORT
#define USBPD_IS_PLUG_IN_OUT_DETECT(port)\
	IS_MASK_SET(IT83XX_USBPD_TCDCR(port), USBPD_REG_PLUG_IN_OUT_DETECT_STAT)
#endif //IT83XX_INTC_PLUG_IN_SUPPORT

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
/* Disable integrated pd module */
void it83xx_disable_pd_module(int port);

#endif /* __CROS_EC_DRIVER_TCPM_IT83XX_H */
