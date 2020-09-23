/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TCPM on ITE chip it8xxx2 with embedded TCPC */

#include "common.h"
#include "config.h"
#include "console.h"
#include "it83xx_pd.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "hooks.h"

#ifdef CONFIG_USB_PD_TCPMV1
#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) || \
	defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) || \
	defined(CONFIG_USB_PD_TCPC_LOW_POWER) || \
	defined(CONFIG_USB_PD_DISCHARGE_TCPC)
#error "Unsupported config options of IT83xx PD driver"
#endif
#endif

#ifdef CONFIG_USB_PD_TCPMV2
#if defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) || \
	defined(CONFIG_USB_PD_DISCHARGE_TCPC)
#error "Unsupported config options of IT83xx PD driver"
#endif
#endif

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Wait time for vconn power switch to turn off. */
#ifndef PD_IT83XX_VCONN_TURN_OFF_DELAY_US
#define PD_IT83XX_VCONN_TURN_OFF_DELAY_US 500
#endif

static uint8_t tx_error_status[IT83XX_USBPD_PHY_PORT_COUNT] = {0};

const struct usbpd_ctrl_t usbpd_ctrl_regs[] = {
	{&IT83XX_GPIO_GPCRF4, &IT83XX_GPIO_GPCRF5, IT83XX_IRQ_USBPD0},
	{&IT83XX_GPIO_GPCRH1, &IT83XX_GPIO_GPCRH2, IT83XX_IRQ_USBPD1},
	{&IT83XX_GPIO_GPCRP0, &IT83XX_GPIO_GPCRP1, IT83XX_IRQ_USBPD2},
};
BUILD_ASSERT(ARRAY_SIZE(usbpd_ctrl_regs) >= IT83XX_USBPD_PHY_PORT_COUNT);

/*
 * Disable cc analog and pd digital module, but only left Rd_5.1K (Not
 * Rd_DB) analog module alive to assert Rd on CCs. EC reset or calling
 * _init() are able to re-active cc and pd.
 */
void it83xx_Rd_5_1K_only_for_hibernate(int port)
{
	uint8_t cc_config = (port == USBPD_PORT_C ?
			     IT83XX_USBPD_CC_PIN_CONFIG2 :
			     IT83XX_USBPD_CC_PIN_CONFIG);

	/* This only apply to active PD port */
	if (*usbpd_ctrl_regs[port].cc1 == cc_config &&
	    *usbpd_ctrl_regs[port].cc2 == cc_config) {
		/* Disable PD Tx and Rx PHY */
		IT83XX_USBPD_PDGCR(port) &= ~USBPD_REG_MASK_BMC_PHY;
		/* Disable CCs voltage detector */
		IT83XX_USBPD_CCGCR(port) |=
			USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR;
		/* Select Rp reserved value for not current leakage */
		IT83XX_USBPD_CCGCR(port) |=
			USBPD_REG_MASK_CC_SELECT_RP_RESERVED;
		/*
		 * Connect CCs analog module (ex.UP/RD/DET/TX/RX), and
		 * connect CCs 5.1K to GND, and
		 * CCs assert Rd
		 */
		IT83XX_USBPD_CCCSR(port) &=
			~(USBPD_REG_MASK_CC2_DISCONNECT |
			  USBPD_REG_MASK_CC2_DISCONNECT_5_1K_TO_GND |
			  USBPD_REG_MASK_CC1_DISCONNECT |
			  USBPD_REG_MASK_CC1_DISCONNECT_5_1K_TO_GND |
			  USBPD_REG_MASK_CC1_CC2_RP_RD_SELECT);
		/* Disconnect CCs 5V tolerant */
		IT83XX_USBPD_CCPSR(port) |=
			(USBPD_REG_MASK_DISCONNECT_POWER_CC2 |
			 USBPD_REG_MASK_DISCONNECT_POWER_CC1);
		/* Enable CCs analog module */
		IT83XX_USBPD_CCGCR(port) &= ~USBPD_REG_MASK_DISABLE_CC;
	}
}

static enum tcpc_cc_voltage_status it83xx_get_cc(
	enum usbpd_port port,
	enum usbpd_cc_pin cc_pin)
{
	enum usbpd_ufp_volt_status ufp_volt;
	enum usbpd_dfp_volt_status dfp_volt;
	enum tcpc_cc_voltage_status cc_state = TYPEC_CC_VOLT_OPEN;

	/*
	 * Because the message header bit(8) field has different definition
	 * between SOP and SOP'/SOP'', in order to not happen misjudgement
	 * when we receive SOP or SOP'/SOP'', the getting power role synchronize
	 * with pd[port].power_role (also synchronization with tcpm_set_cc)
	 * instead of message header.
	 */
	/* Sink */
	if (pd_get_power_role(port) == PD_ROLE_SINK) {
		if (cc_pin == USBPD_CC_PIN_1)
			ufp_volt = USBPD_GET_SNK_COMPARE_CC1_VOLT(port);
		else
			ufp_volt = USBPD_GET_SNK_COMPARE_CC2_VOLT(port);

		switch (ufp_volt) {
		case USBPD_UFP_STATE_SNK_DEF:
			cc_state = TYPEC_CC_VOLT_RP_DEF;
			break;
		case USBPD_UFP_STATE_SNK_1_5:
			cc_state = TYPEC_CC_VOLT_RP_1_5;
			break;
		case USBPD_UFP_STATE_SNK_3_0:
			cc_state = TYPEC_CC_VOLT_RP_3_0;
			break;
		case USBPD_UFP_STATE_SNK_OPEN:
			cc_state = TYPEC_CC_VOLT_OPEN;
			break;
		default:
			cc_state = TYPEC_CC_VOLT_OPEN;
			break;
		}
	/* Source */
	} else {
		if (cc_pin == USBPD_CC_PIN_1)
			dfp_volt = USBPD_GET_SRC_COMPARE_CC1_VOLT(port);
		else
			dfp_volt = USBPD_GET_SRC_COMPARE_CC2_VOLT(port);

		switch (dfp_volt) {
		case USBPD_DFP_STATE_SRC_RA:
			cc_state = TYPEC_CC_VOLT_RA;
			break;
		case USBPD_DFP_STATE_SRC_RD:
			cc_state = TYPEC_CC_VOLT_RD;
			break;
		case USBPD_DFP_STATE_SRC_OPEN:
			cc_state = TYPEC_CC_VOLT_OPEN;
			break;
		default:
			cc_state = TYPEC_CC_VOLT_OPEN;
			break;
		}
	}

	return cc_state;
}

static int it83xx_tcpm_get_message_raw(int port, uint32_t *buf, int *head)
{
	int cnt = PD_HEADER_CNT(IT83XX_USBPD_RMH(port));

	if (!USBPD_IS_RX_DONE(port))
		return EC_ERROR_UNKNOWN;

	/* Store header */
	*head = IT83XX_USBPD_RMH(port);
	/* Check data message */
	if (cnt)
		memcpy(buf, (uint32_t *)&IT83XX_USBPD_RDO(port), cnt * 4);

	return EC_SUCCESS;
}

void it83xx_clear_tx_error_status(enum usbpd_port port)
{
	tx_error_status[port] = 0;
}

void it83xx_get_tx_error_status(enum usbpd_port port)
{
	tx_error_status[port] = IT83XX_USBPD_MTCR(port) &
				(USBPD_REG_MASK_TX_NOT_EN_STAT |
				 USBPD_REG_MASK_TX_DISCARD_STAT |
				 USBPD_REG_MASK_TX_NO_RESPONSE_STAT);
}

static enum tcpc_transmit_complete it83xx_tx_data(
	enum usbpd_port port,
	enum tcpm_transmit_type type,
	uint16_t header,
	const uint32_t *buf)
{
	int r;
	uint32_t evt;
	uint8_t length = PD_HEADER_CNT(header);

	/* Set message header */
	IT83XX_USBPD_MHSR0(port) = (uint8_t)header;
	IT83XX_USBPD_MHSR1(port) = (header >> 8);

	/*
	 * Bit[2:0] Tx message type
	 * 000b=SOP, 001b=SOP', 010b=SOP", 011b=Debug SOP', 100b=Debug SOP''.
	 */
	IT83XX_USBPD_MTSR0(port) =
		(IT83XX_USBPD_MTSR0(port) & ~0x7) | (type & 0x7);

	/* Limited by PD_HEADER_CNT() */
	ASSERT(length <= 0x7);

	if (length)
		/* Set data */
		memcpy((uint32_t *)&IT83XX_USBPD_TDO(port), buf, length * 4);

	for (r = 0; r <= CONFIG_PD_RETRY_COUNT; r++) {
		/* Start Tx */
		USBPD_KICK_TX_START(port);
		evt = task_wait_event_mask(TASK_EVENT_PHY_TX_DONE,
					   PD_T_TCPC_TX_TIMEOUT);

		/*
		 * Check Tx error status (TCPC won't set multi tx errors at one
		 * time transmission):
		 * 1) If we doesn't enable Tx.
		 * 2) If discard, means HW doesn't send the msg and resend.
		 * 3) If port partner doesn't respond GoodCRC.
		 * 4) If Tx timeout.
		 */
		if (tx_error_status[port] || (evt & TASK_EVENT_TIMER)) {
			if (tx_error_status[port] &
					USBPD_REG_MASK_TX_NOT_EN_STAT) {
				CPRINTS("p%d TxErr: Tx EN and resend", port);
				tx_error_status[port] &=
					~USBPD_REG_MASK_TX_NOT_EN_STAT;
				IT83XX_USBPD_PDGCR(port) |=
					USBPD_REG_MASK_TX_MESSAGE_ENABLE;
				continue;
			} else if (tx_error_status[port] &
					USBPD_REG_MASK_TX_DISCARD_STAT) {
				CPRINTS("p%d TxErr: Discard and resend", port);
				tx_error_status[port] &=
					~USBPD_REG_MASK_TX_DISCARD_STAT;
				continue;
			} else if (tx_error_status[port] &
					USBPD_REG_MASK_TX_NO_RESPONSE_STAT) {
				/* HW had automatically resent message twice */
				tx_error_status[port] &=
					~USBPD_REG_MASK_TX_NO_RESPONSE_STAT;
				return TCPC_TX_COMPLETE_FAILED;
			} else if (evt & TASK_EVENT_TIMER) {
				CPRINTS("p%d TxErr: Timeout", port);
				return TCPC_TX_UNSET;
			}
		} else
			break;
	}

	if (r > CONFIG_PD_RETRY_COUNT)
		return TCPC_TX_COMPLETE_DISCARDED;

	return TCPC_TX_COMPLETE_SUCCESS;
}

static enum tcpc_transmit_complete it83xx_send_hw_reset(enum usbpd_port port)
{
	/* Send hard reset */
	USBPD_SEND_HARD_RESET(port);
	usleep(MSEC);

	if (!(IT83XX_USBPD_ISR(port) & USBPD_REG_MASK_HARD_RESET_TX_DONE))
		return TCPC_TX_COMPLETE_FAILED;
	IT83XX_USBPD_ISR(port) = USBPD_REG_MASK_HARD_RESET_TX_DONE;

	return TCPC_TX_COMPLETE_SUCCESS;
}

static enum tcpc_transmit_complete it83xx_send_cable_reset(enum usbpd_port port)
{
	/* Send cable reset */
	USBPD_SEND_CABLE_RESET(port);
	usleep(MSEC);

	if (!(IT83XX_USBPD_ISR(port) & USBPD_REG_MASK_CABLE_RESET_TX_DONE))
		return TCPC_TX_COMPLETE_FAILED;
	IT83XX_USBPD_ISR(port) = USBPD_REG_MASK_CABLE_RESET_TX_DONE;

	return TCPC_TX_COMPLETE_SUCCESS;
}

static void it83xx_send_bist_mode2_pattern(enum usbpd_port port)
{
	USBPD_ENABLE_SEND_BIST_MODE_2(port);
	usleep(PD_T_BIST_TRANSMIT);
	USBPD_DISABLE_SEND_BIST_MODE_2(port);
}

static void it83xx_enable_vconn(enum usbpd_port port, int enabled)
{
	enum usbpd_cc_pin cc_pin;

	if (USBPD_GET_PULL_CC_SELECTION(port))
		cc_pin = USBPD_CC_PIN_1;
	else
		cc_pin = USBPD_CC_PIN_2;

	if (enabled) {
		/* Disable unused CC to become VCONN */
		if (cc_pin == USBPD_CC_PIN_1) {
			IT83XX_USBPD_CCCSR(port) = USBPD_CC2_DISCONNECTED(port);
			IT83XX_USBPD_CCPSR(port) = (IT83XX_USBPD_CCPSR(port)
				& ~USBPD_REG_MASK_DISCONNECT_POWER_CC2)
				| USBPD_REG_MASK_DISCONNECT_POWER_CC1;
		} else {
			IT83XX_USBPD_CCCSR(port) = USBPD_CC1_DISCONNECTED(port);
			IT83XX_USBPD_CCPSR(port) = (IT83XX_USBPD_CCPSR(port)
				& ~USBPD_REG_MASK_DISCONNECT_POWER_CC1)
				| USBPD_REG_MASK_DISCONNECT_POWER_CC2;
		}
	} else {
		/* Connect cc analog module (ex.UP/RD/DET/TX/RX) */
		IT83XX_USBPD_CCCSR(port) &= ~(USBPD_REG_MASK_CC2_DISCONNECT |
					      USBPD_REG_MASK_CC1_DISCONNECT);
		/* Disable cc 5v tolerant */
		IT83XX_USBPD_CCPSR(port) |=
			(USBPD_REG_MASK_DISCONNECT_POWER_CC1 |
			USBPD_REG_MASK_DISCONNECT_POWER_CC2);
	}
}

static void it83xx_enable_cc(enum usbpd_port port, int enable)
{
	if (enable)
		IT83XX_USBPD_CCGCR(port) &= ~USBPD_REG_MASK_DISABLE_CC;
	else
		IT83XX_USBPD_CCGCR(port) |= USBPD_REG_MASK_DISABLE_CC;
}

static void it83xx_set_power_role(enum usbpd_port port, int power_role)
{
	/* 0: PD_ROLE_SINK, 1: PD_ROLE_SOURCE */
	if (power_role == PD_ROLE_SOURCE) {
		/*
		 * Bit[0:6] BMC Rx threshold setting
		 * 000 1000b: power neutral
		 * 010 0000b: sinking power =>
		 *      High to low Y3Rx threshold = 0.38,
		 *      Low to high Y3Rx threshold = 0.54.
		 * 000 0010b: sourcing power =>
		 *      High to low Y3Rx threshold = 0.64,
		 *      Low to high Y3Rx threshold = 0.79.
		 */
		IT83XX_USBPD_BMCDR0(port) = USBPD_REG_MASK_BMC_RX_THRESHOLD_SRC;
		/* Bit0: source */
		IT83XX_USBPD_MHSR1(port) |= USBPD_REG_MASK_SOP_PORT_POWER_ROLE;
		/* Bit1: CC1 and CC2 select Rp */
		IT83XX_USBPD_CCCSR(port) |= USBPD_REG_MASK_CC1_CC2_RP_RD_SELECT;
	} else {
		/*
		 * Bit[0:6] BMC Rx threshold setting
		 * 000 1000b: power neutral
		 * 010 0000b: sinking power =>
		 *      High to low Y3Rx threshold = 0.38,
		 *      Low to high Y3Rx threshold = 0.54.
		 * 000 0010b: sourcing power =>
		 *      High to low Y3Rx threshold = 0.64,
		 *      Low to high Y3Rx threshold = 0.79.
		 */
		IT83XX_USBPD_BMCDR0(port) = USBPD_REG_MASK_BMC_RX_THRESHOLD_SNK;
		/* Bit0: sink */
		IT83XX_USBPD_MHSR1(port) &= ~USBPD_REG_MASK_SOP_PORT_POWER_ROLE;
		/* Bit1: CC1 and CC2 select Rd */
		IT83XX_USBPD_CCCSR(port) &=
			~USBPD_REG_MASK_CC1_CC2_RP_RD_SELECT;
	}
}

static void it83xx_set_data_role(enum usbpd_port port, int data_role)
{
	/* 0: PD_ROLE_UFP 1: PD_ROLE_DFP */
	if (data_role == PD_ROLE_DFP)
		/* Bit5: DFP */
		IT83XX_USBPD_MHSR0(port) |= USBPD_REG_MASK_SOP_PORT_DATA_ROLE;
	else
		/* Bit5: UFP */
		IT83XX_USBPD_MHSR0(port) &= ~USBPD_REG_MASK_SOP_PORT_DATA_ROLE;
}

static void it83xx_select_polarity(enum usbpd_port port,
					enum usbpd_cc_pin cc_pin)
{
	/* CC1/CC2 selection */
	if (cc_pin == USBPD_CC_PIN_1)
		IT83XX_USBPD_CCGCR(port) |= USBPD_REG_MASK_CC1_CC2_SELECTION;
	else
		IT83XX_USBPD_CCGCR(port) &= ~USBPD_REG_MASK_CC1_CC2_SELECTION;
}

static int it83xx_set_cc(enum usbpd_port port, int pull)
{
	int enable_cc = 1;

	switch (pull) {
	case TYPEC_CC_RD:
		it83xx_set_power_role(port, PD_ROLE_SINK);
		break;
	case TYPEC_CC_RP:
		it83xx_set_power_role(port, PD_ROLE_SOURCE);
		break;
	case TYPEC_CC_OPEN:
		/* Power-down CC1 & CC2 to remove Rp/Rd */
		enable_cc = 0;
		break;
	default:
		return EC_ERROR_UNIMPLEMENTED;
	}

	it83xx_enable_cc(port, enable_cc);
	return EC_SUCCESS;
}

static int it83xx_tcpm_release(int port)
{
	return EC_ERROR_UNIMPLEMENTED;
}

static int it83xx_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	*cc2 = it83xx_get_cc(port, USBPD_CC_PIN_2);
	*cc1 = it83xx_get_cc(port, USBPD_CC_PIN_1);

	return EC_SUCCESS;
}

static int it83xx_tcpm_select_rp_value(int port, int rp_sel)
{
	uint8_t rp;

	/*
	 * Bit[3-1]: CC output current (effective when Rp assert in 05h Bit[1])
	 *       111: reserved
	 *       010: 330uA outpt (3.0A)
	 *       100: 180uA outpt (1.5A)
	 *       110: 80uA outpt  (USB default)
	 */
	switch (rp_sel) {
	case TYPEC_RP_1A5:
		rp = USBPD_REG_MASK_CC_SELECT_RP_1A5;
		break;
	case TYPEC_RP_3A0:
		rp = USBPD_REG_MASK_CC_SELECT_RP_3A0;
		break;
	case TYPEC_RP_USB:
	default:
		rp = USBPD_REG_MASK_CC_SELECT_RP_DEF;
		break;
	}
	IT83XX_USBPD_CCGCR(port) = (IT83XX_USBPD_CCGCR(port) & ~(7 << 1)) | rp;

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_cc(int port, int pull)
{
	return it83xx_set_cc(port, pull);
}

static int it83xx_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	enum usbpd_cc_pin cc_pin =
		(polarity == POLARITY_CC1 || polarity == POLARITY_CC1_DTS) ?
		USBPD_CC_PIN_1 : USBPD_CC_PIN_2;

	it83xx_select_polarity(port, cc_pin);

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_vconn(int port, int enable)
{
	/*
	 * IT83XX doesn't have integrated circuit to source CC lines for VCONN.
	 * An external device like PPC or Power Switch has to source the VCONN.
	 */
	if (IS_ENABLED(CONFIG_USBC_VCONN)) {
		if (enable) {
			/*
			 * Unused cc will become Vconn SRC, disable cc analog
			 * module (ex.UP/RD/DET/Tx/Rx) and enable 5v tolerant.
			 */
			it83xx_enable_vconn(port, enable);
			if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
				/* Enable tcpc receive SOP' packet */
				IT83XX_USBPD_PDCSR1(port) |=
					 USBPD_REG_MASK_SOPP_RX_ENABLE;
		}
		/* Turn on/off vconn power switch. */
		board_pd_vconn_ctrl(port,
				    USBPD_GET_PULL_CC_SELECTION(port) ?
				    USBPD_CC_PIN_2 : USBPD_CC_PIN_1, enable);
		if (!enable) {
			if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
				/* Disable tcpc receive SOP' packet */
				IT83XX_USBPD_PDCSR1(port) &=
					~USBPD_REG_MASK_SOPP_RX_ENABLE;
			/*
			 * We need to make sure cc voltage detector is enabled
			 * after vconn is turned off to avoid the potential risk
			 * of voltage fed back into Vcore.
			 */
			usleep(PD_IT83XX_VCONN_TURN_OFF_DELAY_US);
			/*
			 * Since our cc are not Vconn SRC, enable cc analog
			 * module (ex.UP/RD/DET/Tx/Rx) and disable 5v tolerant.
			 */
			it83xx_enable_vconn(port, enable);
		}
	}

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	/* 0: PD_ROLE_SINK, 1: PD_ROLE_SOURCE */
	if (power_role == PD_ROLE_SOURCE)
		/* Bit0: source */
		IT83XX_USBPD_MHSR1(port) |= USBPD_REG_MASK_SOP_PORT_POWER_ROLE;
	else
		/* Bit0: sink */
		IT83XX_USBPD_MHSR1(port) &= ~USBPD_REG_MASK_SOP_PORT_POWER_ROLE;

	it83xx_set_data_role(port, data_role);

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_rx_enable(int port, int enable)
{
	int i;

	if (enable) {
		IT83XX_USBPD_IMR(port) &= ~USBPD_REG_MASK_MSG_RX_DONE;
		USBPD_ENABLE_BMC_PHY(port);
	} else {
		IT83XX_USBPD_IMR(port) |= USBPD_REG_MASK_MSG_RX_DONE;
		USBPD_DISABLE_BMC_PHY(port);
	}

	/*
	 * TCPMv1/TCPMv2 handle SLEEP_MASK_USB_PD and Rx_enable order for deep
	 * sleep mode:
	 * 1.Exit deep sleep mode, Rx enable -> deep sleep disable:
	 * In deep sleep mode, ITE TCPC clock is turned off, so we should
	 * disable deep sleep to leave the mode first then enable Rx, otherwise
	 * we'll miss packet in the mode.
	 * 2.Enter deep sleep mode, deep sleep enable -> Rx disable:
	 * This is OK, but before set Rx disable, our Rx is disabled in deep
	 * sleep mode period.
	 *
	 * So now, we set the SLEEP_MASK_USB_PD only by ITE driver. If any ITE
	 * PD port Rx is enabled, then disable EC deep sleep.
	 */
	for (i = 0; i < CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT; ++i) {
		if (IT83XX_USBPD_PDGCR(i) & USBPD_REG_MASK_BMC_PHY)
			break;
	}

	if (i == board_get_usb_pd_port_count())
		enable_sleep(SLEEP_MASK_USB_PD);
	else
		disable_sleep(SLEEP_MASK_USB_PD);

	return EC_SUCCESS;
}

static int it83xx_tcpm_transmit(int port,
			enum tcpm_transmit_type type,
			uint16_t header,
			const uint32_t *data)
{
	int status = TCPC_TX_COMPLETE_FAILED;

	switch (type) {
	case TCPC_TX_SOP:
	case TCPC_TX_SOP_PRIME:
	case TCPC_TX_SOP_PRIME_PRIME:
	case TCPC_TX_SOP_DEBUG_PRIME:
	case TCPC_TX_SOP_DEBUG_PRIME_PRIME:
		status = it83xx_tx_data(port,
					type,
					header,
					data);
		break;
	case TCPC_TX_BIST_MODE_2:
		it83xx_send_bist_mode2_pattern(port);
		status = TCPC_TX_COMPLETE_SUCCESS;
		break;
	case TCPC_TX_HARD_RESET:
		status = it83xx_send_hw_reset(port);
		break;
	case TCPC_TX_CABLE_RESET:
		status = it83xx_send_cable_reset(port);
		break;
	default:
		status = TCPC_TX_COMPLETE_FAILED;
		break;
	}
	pd_transmit_complete(port, status);

	return EC_SUCCESS;
}

static int it83xx_tcpm_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 *chip_info)
{
	chip_info->vendor_id = USB_VID_ITE;
	chip_info->product_id = ((IT83XX_GCTRL_CHIPID1 << 8) |
				 IT83XX_GCTRL_CHIPID2);
	chip_info->device_id = IT83XX_GCTRL_CHIPVER & 0xf;
	chip_info->fw_version_number = 0xEC;

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int it83xx_tcpm_enter_low_power_mode(int port)
{
	/*
	 * ITE embedded TCPC do low power mode in idle_task(), when all ITE
	 * ports are Rx disabled (means not in Attach.SRC/SNK state or
	 * pd_disabled_mask be set). In deep sleep mode, the timer wakeup PD
	 * task every 5ms, then PD task change the CC lines termination.
	 */
	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_USB_PD_FRS_TCPC
static int it83xx_tcpm_set_frs_enable(int port, int enable)
{
	uint8_t mask = (USBPD_REG_FAST_SWAP_REQUEST_ENABLE |
			USBPD_REG_FAST_SWAP_DETECT_ENABLE);

	if (enable) {
		/*
		 * Disable HW auto turn off FRS requestion and detection
		 * when we receive soft or hard reset.
		 */
		IT83XX_USBPD_PDMSR(port) &= ~USBPD_REG_MASK_AUTO_FRS_DISABLE;
		/* W/C status */
		IT83XX_USBPD_IFS(port) = 0x33;
		/* Enable FRS detection (cc to GND) interrupt */
		IT83XX_USBPD_MIFS(port) &= ~(USBPD_REG_MASK_FAST_SWAP_ISR |
					USBPD_REG_MASK_FAST_SWAP_DETECT_ISR);
		/* Enable FRS detection (cc to GND) */
		IT83XX_USBPD_PDFSCR(port)  = (IT83XX_USBPD_PDFSCR(port) & ~mask)
					| USBPD_REG_FAST_SWAP_DETECT_ENABLE;
		/*
		 * TODO(b/160210457): Enable HW auto trigger
		 * GPH3(port0)/GPH4(port1) output H/L after we detect FRS cc
		 * low signal.
		 */
	} else {
		/* Disable FRS detection (cc to GND) interrupt */
		IT83XX_USBPD_MIFS(port) |= (USBPD_REG_MASK_FAST_SWAP_ISR |
					USBPD_REG_MASK_FAST_SWAP_DETECT_ISR);
		/* Disable FRS detection and requestion */
		IT83XX_USBPD_PDFSCR(port) &= ~mask;
		/*
		 * TODO(b/160210457): Disable HW auto trigger
		 * GPH3(port0)/GPH4(port1) output H/L after we detect FRS cc
		 * low signal.
		 */
	}

	return EC_SUCCESS;
}
#endif

static void it83xx_tcpm_switch_plug_out_type(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/* Check what do we and partner cc assert */
	it83xx_tcpm_get_cc(port, &cc1, &cc2);

	if ((cc1 == TYPEC_CC_VOLT_RD && cc2 == TYPEC_CC_VOLT_RD) ||
	    (cc1 == TYPEC_CC_VOLT_RA && cc2 == TYPEC_CC_VOLT_RA))
		/* We're source, switch to detect audio/debug plug out. */
		IT83XX_USBPD_TCDCR(port) = (IT83XX_USBPD_TCDCR(port) &
				~USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE) |
				USBPD_REG_PLUG_OUT_DETECT_TYPE_SELECT |
				USBPD_REG_PLUG_OUT_SELECT;
	else if (cc1 == TYPEC_CC_VOLT_RD || cc2 == TYPEC_CC_VOLT_RD)
		/* We're source, switch to detect sink plug out. */
		IT83XX_USBPD_TCDCR(port) = (IT83XX_USBPD_TCDCR(port) &
				~USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE &
				~USBPD_REG_PLUG_OUT_DETECT_TYPE_SELECT) |
				USBPD_REG_PLUG_OUT_SELECT;
	else if (cc1 >= TYPEC_CC_VOLT_RP_DEF || cc2 >= TYPEC_CC_VOLT_RP_DEF)
		/*
		 * We're sink, disable detect interrupt, so messages on cc line
		 * won't trigger interrupt.
		 * NOTE: Plug out is detected by TCPM polling Vbus.
		 */
		IT83XX_USBPD_TCDCR(port) |=
			USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE;
	/*
	 * If not above cases, plug in interrupt will fire again,
	 * and call switch_plug_out_type() to set the right state.
	 */
}

void switch_plug_out_type(enum usbpd_port port)
{
	it83xx_tcpm_switch_plug_out_type(port);
}

static void it83xx_init(enum usbpd_port port, int role)
{
	uint8_t cc_config = (port == USBPD_PORT_C ?
			     IT83XX_USBPD_CC_PIN_CONFIG2 :
			     IT83XX_USBPD_CC_PIN_CONFIG);

	/* Reset and disable HW auto generate message header */
	IT83XX_USBPD_PDMSR(port) &= ~USBPD_REG_MASK_DISABLE_AUTO_GEN_TX_HEADER;
	USBPD_SW_RESET(port);
	/* According PD version set HW auto retry count */
	IT83XX_USBPD_PDCSR0(port) = (IT83XX_USBPD_PDCSR0(port) & ~0xC0) |
					(CONFIG_PD_RETRY_COUNT << 6);
	/* Enable rx decode SOP type packet and hard reset signal */
	IT83XX_USBPD_PDCSR1(port) = (USBPD_REG_MASK_HARD_RESET_RX_ENABLE |
				     USBPD_REG_MASK_SOP_RX_ENABLE);
	/* Disable all interrupts */
	IT83XX_USBPD_IMR(port) = 0xff;
	/* W/C status */
	IT83XX_USBPD_ISR(port) = 0xff;
	/* Enable cc voltage detector */
	IT83XX_USBPD_CCGCR(port) &= ~USBPD_REG_MASK_DISABLE_CC_VOL_DETECTOR;
	/* Select Rp value USB-DEFAULT (Rd value default connect with 5.1k) */
	it83xx_tcpm_select_rp_value(port, TYPEC_RP_USB);
	/* Which cc pin connect in attached state. Default to cc1  */
	it83xx_select_polarity(port, USBPD_CC_PIN_1);
	/* Change data role as the same power role */
	it83xx_set_data_role(port, role);
	/* Set default power role and assert Rp/Rd */
	it83xx_set_power_role(port, role);
	/* Disable vconn: connect cc analog module, disable cc 5v tolerant */
	it83xx_enable_vconn(port, 0);
	/* Enable tx done and hard reset detect interrupt */
	IT83XX_USBPD_IMR(port) &= ~(USBPD_REG_MASK_MSG_TX_DONE |
				    USBPD_REG_MASK_HARD_RESET_DETECT);
#ifdef IT83XX_INTC_PLUG_IN_OUT_SUPPORT
	/*
	 * When tcpc detect type-c plug in (cc lines voltage change), it will
	 * interrupt fw to wake pd task, so task can react immediately.
	 *
	 * W/C status and enable type-c plug-in detect interrupt.
	 */
	IT83XX_USBPD_TCDCR(port) = (IT83XX_USBPD_TCDCR(port) &
				    ~(USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE |
				      USBPD_REG_PLUG_OUT_SELECT)) |
				    USBPD_REG_PLUG_IN_OUT_DETECT_STAT;
#endif
	/* Set cc1/cc2 pins alternate mode */
	*usbpd_ctrl_regs[port].cc1 = cc_config;
	*usbpd_ctrl_regs[port].cc2 = cc_config;
	task_clear_pending_irq(usbpd_ctrl_regs[port].irq);
	task_enable_irq(usbpd_ctrl_regs[port].irq);
	USBPD_START(port);
	/*
	 * Disconnect CCs Rd_DB from GND
	 * NOTE: CCs assert both Rd_5.1k and Rd_DB from USBPD_START() to
	 *       disconnect Rd_DB about 1.5us.
	 */
	IT83XX_USBPD_CCPSR(port) |= (USBPD_REG_MASK_DISCONNECT_5_1K_CC2_DB |
				     USBPD_REG_MASK_DISCONNECT_5_1K_CC1_DB);
}

static int it83xx_tcpm_init(int port)
{
	/* Initialize physical layer */
	it83xx_init(port, PD_ROLE_DEFAULT(port));

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPMV2
static void it83xx_tcpm_hook_connect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * There are five cases that hook_connect() be called by TCPMv2:
	 * 1)AttachWait.SNK -> Attached.SNK: disable detect interrupt.
	 * 2)AttachWait.SRC -> Attached.SRC: enable detect plug out.
	 * 3)AttachWait.SNK -> Try.SRC -> TryWait.SNK -> Attached.SNK: we do
	 *   Try.SRC fail, disable detect interrupt.
	 * 4)AttachWait.SNK -> Try.SRC -> Attached.SRC: we do Try.SRC
	 *   successfully, need to switch to detect plug out.
	 * 5)Attached.SRC -> TryWait.SNK -> Attached.SNK: partner do Try.SRC
	 *   successfully, disable detect interrupt.
	 *
	 * NOTE: Try.SRC and TryWait.SNK are embedded respectively in
	 * SRC_DISCONNECT and SNK_DISCONNECT in TCPMv1. Every time we go to
	 * Try.SRC/TryWait.SNK state, the plug in interrupt will be enabled and
	 * fire for 3), 4), 5) cases, then set correctly for the SRC detect plug
	 * out or the SNK disable detect, so TCPMv1 needn't hook connection.
	 */
	it83xx_tcpm_switch_plug_out_type(port);
}

DECLARE_HOOK(HOOK_USB_PD_CONNECT, it83xx_tcpm_hook_connect, HOOK_PRIO_DEFAULT);
#endif

static void it83xx_tcpm_sw_reset(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	if (IS_ENABLED(IT83XX_INTC_PLUG_IN_OUT_SUPPORT))
		/*
		 * Switch to detect plug in and enable detect plug in interrupt,
		 * since pd task has detected a type-c physical disconnected.
		 */
		IT83XX_USBPD_TCDCR(port) &= ~(USBPD_REG_PLUG_OUT_SELECT |
			USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE);

	/* Exit BIST test data mode */
	USBPD_SW_RESET(port);
}

DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, it83xx_tcpm_sw_reset, HOOK_PRIO_DEFAULT);

const struct tcpm_drv it83xx_tcpm_drv = {
	.init			= &it83xx_tcpm_init,
	.release		= &it83xx_tcpm_release,
	.get_cc			= &it83xx_tcpm_get_cc,
	.select_rp_value	= &it83xx_tcpm_select_rp_value,
	.set_cc			= &it83xx_tcpm_set_cc,
	.set_polarity		= &it83xx_tcpm_set_polarity,
	.set_vconn		= &it83xx_tcpm_set_vconn,
	.set_msg_header		= &it83xx_tcpm_set_msg_header,
	.set_rx_enable		= &it83xx_tcpm_set_rx_enable,
	.get_message_raw	= &it83xx_tcpm_get_message_raw,
	.transmit		= &it83xx_tcpm_transmit,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= NULL,
#endif
	.get_chip_info		= &it83xx_tcpm_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &it83xx_tcpm_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable		= &it83xx_tcpm_set_frs_enable,
#endif
};
