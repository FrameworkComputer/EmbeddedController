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
#include "hooks.h"

#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) || \
	defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) || \
	defined(CONFIG_USB_PD_TCPC_LOW_POWER) || \
	defined(CONFIG_USB_PD_DISCHARGE_TCPC)
#error "Unsupported config options of IT83xx PD driver"
#endif

/* Wait time for vconn power switch to turn off. */
#ifndef PD_IT83XX_VCONN_TURN_OFF_DELAY_US
#define PD_IT83XX_VCONN_TURN_OFF_DELAY_US 500
#endif

const struct usbpd_ctrl_t usbpd_ctrl_regs[] = {
	{&IT83XX_GPIO_GPCRF4, &IT83XX_GPIO_GPCRF5, IT83XX_IRQ_USBPD0},
	{&IT83XX_GPIO_GPCRH1, &IT83XX_GPIO_GPCRH2, IT83XX_IRQ_USBPD1},
};
BUILD_ASSERT(ARRAY_SIZE(usbpd_ctrl_regs) == USBPD_PORT_COUNT);

/*
 * This function disables integrated pd module and enables 5.1K resistor for
 * dead battery. A EC reset or calling _init() is able to re-active pd module.
 */
void it83xx_disable_pd_module(int port)
{
	/* This only apply to PD port. */
	if (*usbpd_ctrl_regs[port].cc1 == IT83XX_USBPD_CC_PIN_CONFIG &&
		*usbpd_ctrl_regs[port].cc2 == IT83XX_USBPD_CC_PIN_CONFIG) {
		/* Disable PD PHY */
		IT83XX_USBPD_GCR(port) &= ~(BIT(0) | BIT(4));
		/* Power down CC1/CC2 */
		IT83XX_USBPD_CCGCR(port) |= 0x1f;
		/* Disable CC1/CC2 voltage detector */
		IT83XX_USBPD_CCCSR(port) = 0xff;
		/* Connect 5.1K resistor to CC1/CC2 for dead battery. */
		IT83XX_USBPD_CCPSR(port) = 0x33;
	}
}

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
		CLEAR_MASK(cc_state, BIT(2));
	/* select Rd */
	else
		SET_MASK(cc_state, BIT(2));

	/* sink */
	if (USBPD_GET_POWER_ROLE(port) == USBPD_POWER_ROLE_CONSUMER) {
		if (cc_pin == USBPD_CC_PIN_1)
			ufp_volt = IT83XX_USBPD_UFPVDR(port) & 0x7;
		else
			ufp_volt = (IT83XX_USBPD_UFPVDR(port) >> 4) & 0x7;

		switch (ufp_volt) {
		case USBPD_UFP_STATE_SNK_DEF:
			cc_state |= (TYPEC_CC_VOLT_RP_DEF & 3);
			break;
		case USBPD_UFP_STATE_SNK_1_5:
			cc_state |= (TYPEC_CC_VOLT_RP_1_5 & 3);
			break;
		case USBPD_UFP_STATE_SNK_3_0:
			cc_state |= (TYPEC_CC_VOLT_RP_3_0 & 3);
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

static int it83xx_tcpm_get_message_raw(int port, uint32_t *buf, int *head)
{
	int cnt = PD_HEADER_CNT(IT83XX_USBPD_RMH(port));

	if (!USBPD_IS_RX_DONE(port))
		return EC_ERROR_UNKNOWN;

	/* store header */
	*head = IT83XX_USBPD_RMH(port);
	/* check data message */
	if (cnt)
		memcpy(buf, (uint32_t *)&IT83XX_USBPD_RDO0(port), cnt * 4);
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
	uint16_t header,
	const uint32_t *buf)
{
	int r;
	uint32_t evt;
	uint8_t length = PD_HEADER_CNT(header);

	/* set message header */
	IT83XX_USBPD_TMHLR(port) = (uint8_t)header;
	IT83XX_USBPD_TMHHR(port) = (header >> 8);

	/*
	 * SOP type bit[6~4]:
	 * on bx version and before:
	 * x00b=SOP, x01b=SOP', x10b=SOP", bit[6] is reserved.
	 * on dx version:
	 * 000b=SOP, 001b=SOP', 010b=SOP", 011b=Debug SOP', 100b=Debug SOP''.
	 */
	IT83XX_USBPD_MTSR1(port) =
		(IT83XX_USBPD_MTSR1(port) & ~0x70) | ((type & 0x7) << 4);
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
		IT83XX_USBPD_MTSR0(port) |= BIT(4);
		/* set data length setting */
		IT83XX_USBPD_MTSR1(port) |= length;
		/* set data */
		memcpy((uint32_t *)&IT83XX_USBPD_TDO(port), buf, length * 4);
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
			/*
			 * Or port partner doesn't respond GoodCRC
			 */
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
		/* Enable cc1 and cc2 */
		IT83XX_USBPD_CCCSR(port) &= ~0xaa;
		IT83XX_USBPD_CCPSR(port) |=
			(USBPD_REG_MASK_DISCONNECT_POWER_CC1 |
			USBPD_REG_MASK_DISCONNECT_POWER_CC2);
	}
}

static void it83xx_enable_cc(enum usbpd_port port, int enable)
{
	if (enable)
		CLEAR_MASK(IT83XX_USBPD_CCGCR(port), BIT(4));
	else
		SET_MASK(IT83XX_USBPD_CCGCR(port), BIT(4));
}

static void it83xx_set_power_role(enum usbpd_port port, int power_role)
{
	/* PD_ROLE_SINK 0, PD_ROLE_SOURCE 1 */
	if (power_role == PD_ROLE_SOURCE) {
		/*
		 * bit[2,3] BMC Rx threshold setting
		 * 00b: power neutral
		 * 01b: sinking power =>
		 *      High to low Y3Rx threshold = 0.38,
		 *      Low to high Y3Rx threshold = 0.54.
		 * 10b: sourcing power =>
		 *      High to low Y3Rx threshold = 0.64,
		 *      Low to high Y3Rx threshold = 0.79.
		 */
		IT83XX_USBPD_CCADCR(port) = 0x08;
		/* bit0: source */
		SET_MASK(IT83XX_USBPD_PDMSR(port), BIT(0));
		/* bit1: CC1 select Rp */
		SET_MASK(IT83XX_USBPD_CCGCR(port), BIT(1));
		/* bit3: CC2 select Rp */
		SET_MASK(IT83XX_USBPD_BMCSR(port), BIT(3));
	} else {
		/*
		 * bit[2,3] BMC Rx threshold setting
		 * 00b: power neutral
		 * 01b: sinking power =>
		 *      High to low Y3Rx threshold = 0.38,
		 *      Low to high Y3Rx threshold = 0.54
		 * 10b: sourcing power =>
		 *      High to low Y3Rx threshold = 0.64,
		 *      Low to high Y3Rx threshold = 0.79
		 */
		IT83XX_USBPD_CCADCR(port) = 0x04;
		/* bit0: sink */
		CLEAR_MASK(IT83XX_USBPD_PDMSR(port), BIT(0));
		/* bit1: CC1 select Rd */
		CLEAR_MASK(IT83XX_USBPD_CCGCR(port), BIT(1));
		/* bit3: CC2 select Rd */
		CLEAR_MASK(IT83XX_USBPD_BMCSR(port), BIT(3));
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
#ifdef IT83XX_USBPD_CC_PARAMETER_RELOAD
	/* bit7: Reload CC parameter setting. */
	IT83XX_USBPD_CCPSR0(port) |= BIT(7);
#endif
	/* reset and disable HW auto generate message header */
	IT83XX_USBPD_GCR(port) = BIT(5);
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
#ifdef IT83XX_INTC_PLUG_IN_SUPPORT
	/*
	 * when tcpc detect type-c plug in (cc lines voltage change), it will
	 * interrupt fw to wake pd task, so task can react immediately.
	 *
	 * w/c status and unmask TCDCR (detect type-c plug in interrupt default
	 * is enable).
	 */
	IT83XX_USBPD_TCDCR(port) = USBPD_REG_PLUG_IN_OUT_DETECT_STAT;
#endif //IT83XX_INTC_PLUG_IN_SUPPORT
	IT83XX_USBPD_CCPSR(port) = 0xff;
	/* cc connect */
	IT83XX_USBPD_CCCSR(port) = 0;
	/* disable vconn */
	it83xx_enable_vconn(port, 0);
	/* TX start from high */
	IT83XX_USBPD_CCADCR(port) |= BIT(6);
	/* enable cc1/cc2 */
	*usbpd_ctrl_regs[port].cc1 = IT83XX_USBPD_CC_PIN_CONFIG;
	*usbpd_ctrl_regs[port].cc2 = IT83XX_USBPD_CC_PIN_CONFIG;
	task_clear_pending_irq(usbpd_ctrl_regs[port].irq);
	task_enable_irq(usbpd_ctrl_regs[port].irq);
	USBPD_START(port);
}

static void it83xx_select_polarity(enum usbpd_port port,
					enum usbpd_cc_pin cc_pin)
{
	/* cc1/cc2 selection */
	if (cc_pin == USBPD_CC_PIN_1)
		SET_MASK(IT83XX_USBPD_CCGCR(port), BIT(0));
	else
		CLEAR_MASK(IT83XX_USBPD_CCGCR(port), BIT(0));
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

static int it83xx_tcpm_init(int port)
{
	/* Initialize physical layer */
	it83xx_init(port, PD_ROLE_DEFAULT(port));

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
		rp = BIT(2);
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
	return it83xx_set_cc(port, pull);
}

static int it83xx_tcpm_set_polarity(int port, int polarity)
{
	it83xx_select_polarity(port, polarity);

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
				IT83XX_USBPD_PDMSR(port) |=
					USBPD_REG_MASK_SOPP_ENABLE;
		}

		/* Turn on/off vconn power switch. */
		board_pd_vconn_ctrl(port,
			USBPD_GET_PULL_CC_SELECTION(port) ?
				USBPD_CC_PIN_2 : USBPD_CC_PIN_1, enable);

		if (!enable) {
			/* Disable tcpc receive SOP' packet */
			if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
				IT83XX_USBPD_PDMSR(port) &=
					~USBPD_REG_MASK_SOPP_ENABLE;
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
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; ++i)
		if (IT83XX_USBPD_GCR(i) | USBPD_REG_MASK_BMC_PHY)
			break;

	if (i == CONFIG_USB_PD_PORT_MAX_COUNT)
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

static int it83xx_tcpm_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 **chip_info)
{
	static struct ec_response_pd_chip_info_v1 i;

	*chip_info = &i;
	i.vendor_id = USB_VID_ITE;
	i.product_id = (IT83XX_GCTRL_CHIPID1 << 8) | IT83XX_GCTRL_CHIPID2;
	i.device_id = IT83XX_GCTRL_CHIPVER & 0xf;
	i.fw_version_number = 0xEC;

	return EC_SUCCESS;
}

static void it83xx_tcpm_sw_reset(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());
#ifdef IT83XX_INTC_PLUG_IN_SUPPORT
	/*
	 * Enable detect type-c plug in interrupt, since the pd task has
	 * detected a type-c physical disconnected.
	 */
	IT83XX_USBPD_TCDCR(port) &= ~USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE;
#endif //IT83XX_INTC_PLUG_IN_SUPPORT
	/* exit BIST test data mode */
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
	.get_chip_info		= &it83xx_tcpm_get_chip_info,
};
