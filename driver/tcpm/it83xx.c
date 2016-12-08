/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TCPM for MCU also running TCPC */

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

const struct usbpd_ctrl_t usbpd_ctrl_regs[] = {
	{&IT83XX_GPIO_GPCRF4, &IT83XX_GPIO_GPCRF5, IT83XX_IRQ_USBPD0},
	{&IT83XX_GPIO_GPCRH1, &IT83XX_GPIO_GPCRH2, IT83XX_IRQ_USBPD1},
};
BUILD_ASSERT(ARRAY_SIZE(usbpd_ctrl_regs) == USBPD_PORT_COUNT);

static enum tcpc_cc_voltage_status it83xx_get_cc(
	enum usbpd_port port,
	enum usbpd_cc_pin cc_pin)
{
	enum usbpd_ufp_volt_status ufp_volt;
	enum usbpd_dfp_volt_status dfp_volt;
	enum tcpc_cc_voltage_status cc_state = TYPEC_CC_VOLT_OPEN;
	int pull;

	pull = (cc_pin == USBPD_CC_PIN_1) ?
		USBPD_GET_CC1_PULL_REGISTER_SELECTION(port) :
		USBPD_GET_CC2_PULL_REGISTER_SELECTION(port);

	/* select Rp */
	if (pull)
		CLEAR_MASK(cc_state, (1 << 2));
	/* select Rd */
	else
		SET_MASK(cc_state, (1 << 2));

	/* sink */
	if (USBPD_GET_POWER_ROLE(port) == USBPD_POWER_ROLE_CONSUMER) {
		if (cc_pin == USBPD_CC_PIN_1)
			ufp_volt = IT83XX_USBPD_UFPVDR(port) & 0xf;
		else
			ufp_volt = (IT83XX_USBPD_UFPVDR(port) >> 4) & 0xf;

		switch (ufp_volt) {
		case USBPD_UFP_STATE_SNK_DEF:
			cc_state |= (TYPEC_CC_VOLT_SNK_DEF & 3);
			break;
		case USBPD_UFP_STATE_SNK_1_5:
			cc_state |= (TYPEC_CC_VOLT_SNK_1_5 & 3);
			break;
		case USBPD_UFP_STATE_SNK_3_0:
			cc_state |= (TYPEC_CC_VOLT_SNK_3_0 & 3);
			break;
		case USBPD_UFP_STATE_SNK_OPEN:
			cc_state = TYPEC_CC_VOLT_OPEN;
			break;
		default:
			cc_state = TYPEC_CC_VOLT_OPEN;
			break;
		}
	/* source */
	} else {
		if (cc_pin == USBPD_CC_PIN_1)
			dfp_volt = IT83XX_USBPD_DFPVDR(port) & 0xf;
		else
			dfp_volt = (IT83XX_USBPD_DFPVDR(port) >> 4) & 0xf;

		switch (dfp_volt) {
		case USBPD_DFP_STATE_SRC_RA:
			cc_state |= TYPEC_CC_VOLT_RA;
			break;
		case USBPD_DFP_STATE_SRC_RD:
			cc_state |= TYPEC_CC_VOLT_RD;
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

static int it83xx_rx_data(enum usbpd_port port, int *head, uint32_t *buf)
{
	struct usbpd_header *p_head = (struct usbpd_header *)head;

	if (!USBPD_IS_RX_DONE(port))
		return EC_ERROR_UNKNOWN;

	/* store header */
	*p_head = *((struct usbpd_header *)IT83XX_USBPD_RMH_BASE(port));
	/* check data message */
	if (p_head->data_obj_num)
		memcpy(buf,
			(uint8_t *)IT83XX_USBPD_RDO_BASE(port),
			p_head->data_obj_num * 4);
	/*
	 * Note: clear RX done interrupt after get the data.
	 * If clear this bit, USBPD receives next packet
	 */
	IT83XX_USBPD_MRSR(port) = USBPD_REG_MASK_RX_MSG_VALID;

	return EC_SUCCESS;
}

static enum tcpc_transmit_complete it83xx_tx_data(
	enum usbpd_port port,
	enum tcpm_transmit_type type,
	uint8_t msg_type,
	uint8_t length,
	const uint32_t *buf)
{
	int r;
	uint32_t evt;

	/* set message type */
	IT83XX_USBPD_MTSR0(port) =
		(IT83XX_USBPD_MTSR0(port) & ~0x1f) | (msg_type & 0xf);
	/* SOP type: bit[5:4] 00 SOP, 01 SOP', 10 SOP" */
	IT83XX_USBPD_MTSR1(port) =
		(IT83XX_USBPD_MTSR1(port) & ~0x30) | ((type & 0x3) << 4);
	/* bit7: transmit message is send to cable or not */
	if (TCPC_TX_SOP == type)
		IT83XX_USBPD_MTSR0(port) &= ~USBPD_REG_MASK_CABLE_ENABLE;
	else
		IT83XX_USBPD_MTSR0(port) |= USBPD_REG_MASK_CABLE_ENABLE;
	/* clear msg length */
	IT83XX_USBPD_MTSR1(port) &= (~0x7);
	/* Limited by PD_HEADER_CNT() */
	ASSERT(length <= 0x7);

	if (length) {
		/* set data bit */
		IT83XX_USBPD_MTSR0(port) |= (1 << 4);
		/* set data length setting */
		IT83XX_USBPD_MTSR1(port) |= length;
		/* set data */
		memcpy((uint8_t *)IT83XX_USBPD_TDO_BASE(port), buf, length * 4);
	}

	for (r = 0; r <= PD_RETRY_COUNT; r++) {
		/* Start TX */
		USBPD_KICK_TX_START(port);
		evt = task_wait_event_mask(TASK_EVENT_PHY_TX_DONE,
					PD_T_TCPC_TX_TIMEOUT);
		/* check TX status */
		if (USBPD_IS_TX_ERR(port) || (evt & TASK_EVENT_TIMER)) {
			/*
			 * If discard, means HW doesn't send the msg and resend.
			 */
			if (USBPD_IS_TX_DISCARD(port))
				continue;
			else
				return TCPC_TX_COMPLETE_FAILED;
		} else {
			break;
		}
	}

	if (r > PD_RETRY_COUNT)
		return TCPC_TX_COMPLETE_DISCARDED;

	return TCPC_TX_COMPLETE_SUCCESS;
}

static enum tcpc_transmit_complete it83xx_send_hw_reset(enum usbpd_port port,
				enum tcpm_transmit_type reset_type)
{
	if (reset_type == TCPC_TX_CABLE_RESET)
		IT83XX_USBPD_MTSR0(port) |= USBPD_REG_MASK_CABLE_ENABLE;
	else
		IT83XX_USBPD_MTSR0(port) &= ~USBPD_REG_MASK_CABLE_ENABLE;

	/* send hard reset */
	USBPD_SEND_HARD_RESET(port);
	usleep(MSEC);

	if (IT83XX_USBPD_MTSR0(port) & USBPD_REG_MASK_SEND_HW_RESET)
		return TCPC_TX_COMPLETE_FAILED;

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
			IT83XX_USBPD_CCCSR(port) =
				(IT83XX_USBPD_CCCSR(port) | 0xa0) & ~0xa;
			IT83XX_USBPD_CCPSR(port) = (IT83XX_USBPD_CCPSR(port)
				& ~USBPD_REG_MASK_DISCONNECT_POWER_CC2)
				| USBPD_REG_MASK_DISCONNECT_POWER_CC1;
		} else {
			IT83XX_USBPD_CCCSR(port) =
				(IT83XX_USBPD_CCCSR(port) | 0xa) & ~0xa0;
			IT83XX_USBPD_CCPSR(port) = (IT83XX_USBPD_CCPSR(port)
				& ~USBPD_REG_MASK_DISCONNECT_POWER_CC1)
				| USBPD_REG_MASK_DISCONNECT_POWER_CC2;
		}
	} else {
		/* Enable cc1 and cc2 */
		IT83XX_USBPD_CCCSR(port) &= ~0xaa;
		IT83XX_USBPD_CCPSR(port) |=
			(USBPD_REG_MASK_DISCONNECT_POWER_CC1 |
			USBPD_REG_MASK_DISCONNECT_POWER_CC2);
	}
}

static void it83xx_set_power_role(enum usbpd_port port, int power_role)
{
	/* PD_ROLE_SINK 0, PD_ROLE_SOURCE 1 */
	if (power_role == PD_ROLE_SOURCE) {
		/* bit0: source */
		SET_MASK(IT83XX_USBPD_PDMSR(port), (1 << 0));
		/* bit1: CC1 select Rp */
		SET_MASK(IT83XX_USBPD_CCGCR(port), (1 << 1));
		/* bit3: CC2 select Rp */
		SET_MASK(IT83XX_USBPD_BMCSR(port), (1 << 3));
	} else {
		/* bit0: sink */
		CLEAR_MASK(IT83XX_USBPD_PDMSR(port), (1 << 0));
		/* bit1: CC1 select Rd */
		CLEAR_MASK(IT83XX_USBPD_CCGCR(port), (1 << 1));
		/* bit3: CC2 select Rd */
		CLEAR_MASK(IT83XX_USBPD_BMCSR(port), (1 << 3));
	}
}

static void it83xx_set_data_role(enum usbpd_port port, int pd_role)
{
	/* 0: PD_ROLE_UFP 1: PD_ROLE_DFP */
	IT83XX_USBPD_PDMSR(port) =
		(IT83XX_USBPD_PDMSR(port) & ~0xc) | ((pd_role & 0x1) << 2);
}

static void it83xx_init(enum usbpd_port port, int role)
{
	/* reset */
	IT83XX_USBPD_GCR(port) = 0;
	USBPD_SW_RESET(port);
	/* set SOP: receive SOP message only.
	 * bit[7]: SOP" support enable.
	 * bit[6]: SOP' support enable.
	 * bit[5]: SOP  support enable.
	 */
	IT83XX_USBPD_PDMSR(port) = USBPD_REG_MASK_SOP_ENABLE;
	/* W/C status */
	IT83XX_USBPD_ISR(port) = 0xff;
	/* enable cc, select cc1 and Rd. */
	IT83XX_USBPD_CCGCR(port) = 0xd;
	/* change data role as the same power role */
	it83xx_set_data_role(port, role);
	/* set power role */
	it83xx_set_power_role(port, role);
	/* disable all interrupts */
	IT83XX_USBPD_IMR(port) = 0xff;
	/* enable tx done and reset detect interrupt */
	IT83XX_USBPD_IMR(port) &= ~(USBPD_REG_MASK_MSG_TX_DONE |
					USBPD_REG_MASK_HARD_RESET_DETECT);
	IT83XX_USBPD_CCPSR(port) = 0xff;
	/* cc connect */
	IT83XX_USBPD_CCCSR(port) = 0;
	/* disable vconn */
	it83xx_enable_vconn(port, 0);
	/* TX start from high */
	IT83XX_USBPD_CCADCR(port) |= (1 << 6);
	/* enable cc1/cc2 */
	*usbpd_ctrl_regs[port].cc1 = 0x86;
	*usbpd_ctrl_regs[port].cc2 = 0x86;
	task_clear_pending_irq(usbpd_ctrl_regs[port].irq);
	task_enable_irq(usbpd_ctrl_regs[port].irq);
	USBPD_START(port);
}

static void it83xx_select_polarity(enum usbpd_port port,
					enum usbpd_cc_pin cc_pin)
{
	/* cc1/cc2 selection */
	if (cc_pin == USBPD_CC_PIN_1)
		SET_MASK(IT83XX_USBPD_CCGCR(port), (1 << 0));
	else
		CLEAR_MASK(IT83XX_USBPD_CCGCR(port), (1 << 0));
}

static void it83xx_set_cc(enum usbpd_port port, int pull)
{
	if (pull == TYPEC_CC_RD)
		it83xx_set_power_role(port, PD_ROLE_SINK);
	else if (pull == TYPEC_CC_RP)
		it83xx_set_power_role(port, PD_ROLE_SOURCE);
}

static int it83xx_tcpm_init(int port)
{
	/* Initialize physical layer */
	it83xx_init(port, PD_ROLE_DEFAULT);

	return EC_SUCCESS;
}

static int it83xx_tcpm_get_cc(int port, int *cc1, int *cc2)
{
	*cc2 = it83xx_get_cc(port, USBPD_CC_PIN_2);
	*cc1 = it83xx_get_cc(port, USBPD_CC_PIN_1);

	return EC_SUCCESS;
}

static int it83xx_tcpm_select_rp_value(int port, int rp_sel)
{
	uint8_t rp;
	/*
	 * bit[3-2]: CC output current (when Rp selected)
	 *       00: reserved
	 *       01: 330uA outpt (3.0A)
	 *       10: 180uA outpt (1.5A)
	 *       11: 80uA outpt  (USB default)
	 */
	switch (rp_sel) {
	case TYPEC_RP_1A5:
		rp = 2 << 2;
		break;
	case TYPEC_RP_3A0:
		rp = 1 << 2;
		break;
	case TYPEC_RP_USB:
	default:
		rp = 3 << 2;
		break;
	}
	IT83XX_USBPD_CCGCR(port) = (IT83XX_USBPD_CCGCR(port) & ~(3 << 2)) | rp;

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_cc(int port, int pull)
{
	it83xx_set_cc(port, pull);

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_polarity(int port, int polarity)
{
	it83xx_select_polarity(port, polarity);

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_vconn(int port, int enable)
{
#ifdef CONFIG_USBC_VCONN
	it83xx_enable_vconn(port, enable);
	/* vconn switch */
	board_pd_vconn_ctrl(port,
		USBPD_GET_PULL_CC_SELECTION(port) ?
				USBPD_CC_PIN_2 :
				USBPD_CC_PIN_1, enable);
#endif

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	it83xx_set_power_role(port, power_role);
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

	/* If any PD port is connected, then disable deep sleep */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; ++i)
		if (IT83XX_USBPD_GCR(i) | USBPD_REG_MASK_BMC_PHY)
			break;

	if (i == CONFIG_USB_PD_PORT_COUNT)
		enable_sleep(SLEEP_MASK_USB_PD);
	else
		disable_sleep(SLEEP_MASK_USB_PD);

	return EC_SUCCESS;
}

static int it83xx_tcpm_get_message(int port, uint32_t *payload, int *head)
{
	int ret = it83xx_rx_data(port, head, payload);
	/* un-mask RX done interrupt */
	IT83XX_USBPD_IMR(port) &= ~USBPD_REG_MASK_MSG_RX_DONE;

	return ret;
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
		status = it83xx_tx_data(port,
					type,
					PD_HEADER_TYPE(header),
					PD_HEADER_CNT(header),
					data);
		break;
	case TCPC_TX_BIST_MODE_2:
		it83xx_send_bist_mode2_pattern(port);
		status = TCPC_TX_COMPLETE_SUCCESS;
		break;
	case TCPC_TX_HARD_RESET:
	case TCPC_TX_CABLE_RESET:
		status = it83xx_send_hw_reset(port, type);
		break;
	default:
		status = TCPC_TX_COMPLETE_FAILED;
		break;
	}
	pd_transmit_complete(port, status);

	return EC_SUCCESS;
}

const struct tcpm_drv it83xx_tcpm_drv = {
	.init			= &it83xx_tcpm_init,
	.get_cc			= &it83xx_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= NULL,
#endif
	.select_rp_value	= &it83xx_tcpm_select_rp_value,
	.set_cc			= &it83xx_tcpm_set_cc,
	.set_polarity		= &it83xx_tcpm_set_polarity,
	.set_vconn		= &it83xx_tcpm_set_vconn,
	.set_msg_header		= &it83xx_tcpm_set_msg_header,
	.set_rx_enable		= &it83xx_tcpm_set_rx_enable,
	.get_message		= &it83xx_tcpm_get_message,
	.transmit		= &it83xx_tcpm_transmit,
	.tcpc_alert		= NULL,
};
