/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TCPM for MCU also running TCPC */

#include "builtin/assert.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "hooks.h"
#include "it83xx_pd.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#ifdef CONFIG_USB_PD_TCPMV1
#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) || \
	defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) ||  \
	defined(CONFIG_USB_PD_TCPC_LOW_POWER) ||    \
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

int rx_en[IT83XX_USBPD_PHY_PORT_COUNT];
STATIC_IF(CONFIG_USB_PD_DECODE_SOP)
bool sop_prime_en[IT83XX_USBPD_PHY_PORT_COUNT];

const struct usbpd_ctrl_t usbpd_ctrl_regs[] = {
	{ &IT83XX_GPIO_GPCRF4, &IT83XX_GPIO_GPCRF5, IT83XX_IRQ_USBPD0 },
	{ &IT83XX_GPIO_GPCRH1, &IT83XX_GPIO_GPCRH2, IT83XX_IRQ_USBPD1 },
};
BUILD_ASSERT(ARRAY_SIZE(usbpd_ctrl_regs) == IT83XX_USBPD_PHY_PORT_COUNT);

static int it83xx_tcpm_set_rx_enable(int port, int enable);
static int it83xx_tcpm_set_vconn(int port, int enable);

/*
 * Disable cc analog and pd digital module, but only left Rd_5.1K (Not
 * Dead Battery) analog module alive to assert Rd on CCs. EC reset or
 * calling _init() are able to re-active cc and pd.
 */
void it83xx_Rd_5_1K_only_for_hibernate(int port)
{
	/* This only apply to active PD port */
	if (*usbpd_ctrl_regs[port].cc1 == IT83XX_USBPD_CC_PIN_CONFIG &&
	    *usbpd_ctrl_regs[port].cc2 == IT83XX_USBPD_CC_PIN_CONFIG) {
		/* Disable PD PHY */
		IT83XX_USBPD_GCR(port) &= ~(BIT(0) | BIT(4));
		/*
		 * Disable CCs voltage detector, and
		 * connect CCs analog module (ex.UP/RD/DET/TX/RX), and
		 * connect CCs 5.1K to GND
		 */
		IT83XX_USBPD_CCCSR(port) = 0x22;
		/* Disconnect CCs 5V tolerant */
		IT83XX_USBPD_CCPSR(port) |=
			(USBPD_REG_MASK_DISCONNECT_POWER_CC2 |
			 USBPD_REG_MASK_DISCONNECT_POWER_CC1);
		/*
		 * Select Rp reserved value for not current leakage, and
		 * CCs assert Rd, and
		 * enable CCs analog module
		 */
		IT83XX_USBPD_BMCSR(port) &= ~0x08;
		IT83XX_USBPD_CCGCR(port) &= ~0x1f;
	}
}

static enum tcpc_cc_voltage_status it83xx_get_cc(enum usbpd_port port,
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

	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP)) {
		int type = USBPD_REG_GET_SOP_TYPE_RX(IT83XX_USBPD_MRSR(port));
		*head |= PD_HEADER_SOP(type);
	}
	/*
	 * Note: clear RX done interrupt after get the data.
	 * If clear this bit, USBPD receives next packet
	 */
	IT83XX_USBPD_MRSR(port) = USBPD_REG_MASK_RX_MSG_VALID;

	return EC_SUCCESS;
}

static enum tcpc_transmit_complete it83xx_tx_data(enum usbpd_port port,
						  enum tcpci_msg_type type,
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
	IT83XX_USBPD_MTSR1(port) = (IT83XX_USBPD_MTSR1(port) & ~0x70) |
				   ((type & 0x7) << 4);
	/* bit7: transmit message is send to cable or not */
	if (type == TCPCI_MSG_SOP)
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

	for (r = 0; r <= CONFIG_PD_RETRY_COUNT; r++) {
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

	if (r > CONFIG_PD_RETRY_COUNT)
		return TCPC_TX_COMPLETE_DISCARDED;

	return TCPC_TX_COMPLETE_SUCCESS;
}

static enum tcpc_transmit_complete
it83xx_send_hw_reset(enum usbpd_port port, enum tcpci_msg_type reset_type)
{
	if (reset_type == TCPCI_MSG_CABLE_RESET)
		IT83XX_USBPD_MTSR0(port) |= USBPD_REG_MASK_CABLE_ENABLE;
	else
		IT83XX_USBPD_MTSR0(port) &= ~USBPD_REG_MASK_CABLE_ENABLE;

	/* send hard reset */
	USBPD_SEND_HARD_RESET(port);
	crec_usleep(MSEC);

	if (USBPD_IS_HARD_CABLE_RESET_TX_DONE(port)) {
		IT83XX_USBPD_ISR(port) =
			USBPD_REG_MASK_HARD_CABLE_RESET_TX_DONE;
		return TCPC_TX_COMPLETE_SUCCESS;
	}

	return TCPC_TX_COMPLETE_FAILED;
}

static void it83xx_send_bist_mode2_pattern(enum usbpd_port port)
{
	USBPD_ENABLE_SEND_BIST_MODE_2(port);
	crec_usleep(PD_T_BIST_TRANSMIT);
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
			IT83XX_USBPD_CCPSR(port) =
				(IT83XX_USBPD_CCPSR(port) &
				 ~USBPD_REG_MASK_DISCONNECT_POWER_CC2) |
				USBPD_REG_MASK_DISCONNECT_POWER_CC1;
		} else {
			IT83XX_USBPD_CCCSR(port) = USBPD_CC1_DISCONNECTED(port);
			IT83XX_USBPD_CCPSR(port) =
				(IT83XX_USBPD_CCPSR(port) &
				 ~USBPD_REG_MASK_DISCONNECT_POWER_CC1) |
				USBPD_REG_MASK_DISCONNECT_POWER_CC2;
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
	IT83XX_USBPD_PDMSR(port) = (IT83XX_USBPD_PDMSR(port) & ~0xc) |
				   ((pd_role & 0x1) << 2);
}

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
		IT83XX_USBPD_PDPSR(port) &= ~USBPD_REG_MASK_AUTO_FRS_DISABLE;
		/* W/C status */
		IT83XX_USBPD_PD30IR(port) = 0x3f;
		/* Enable FRS detection (cc to GND) interrupt */
		IT83XX_USBPD_MPD30IR(port) &=
			~(USBPD_REG_MASK_PD30_ISR |
			  USBPD_REG_MASK_FAST_SWAP_DETECT_ISR);
		/* Enable FRS detection (cc to GND) */
		IT83XX_USBPD_PDQSCR(port) =
			(IT83XX_USBPD_PDQSCR(port) & ~mask) |
			USBPD_REG_FAST_SWAP_DETECT_ENABLE;
	} else {
		/* Disable FRS detection (cc to GND) interrupt */
		IT83XX_USBPD_MPD30IR(port) |=
			(USBPD_REG_MASK_PD30_ISR |
			 USBPD_REG_MASK_FAST_SWAP_DETECT_ISR);
		/* Disable FRS detection and requestion */
		IT83XX_USBPD_PDQSCR(port) &= ~mask;
	}

	return EC_SUCCESS;
}
#endif

static void it83xx_init(enum usbpd_port port, int role)
{
#ifdef IT83XX_USBPD_CC_PARAMETER_RELOAD
	/* bit7: Reload CC parameter setting. */
	IT83XX_USBPD_CCPSR0(port) |= BIT(7);
#endif
	/* reset and disable HW auto generate message header */
	IT83XX_USBPD_GCR(port) = BIT(5);
	USBPD_SW_RESET(port);
	/*
	 * According PD version set the total number of HW attempts
	 * (= retry count + 1)
	 */
	IT83XX_USBPD_BMCSR(port) = (IT83XX_USBPD_BMCSR(port) & ~0x70) |
				   ((CONFIG_PD_RETRY_COUNT + 1) << 4);
	/* Disable Rx decode */
	it83xx_tcpm_set_rx_enable(port, 0);
	if (IS_ENABLED(CONFIG_USB_PD_TCPMV1)) {
		uint8_t flags = 0;
		/*
		 * If explicit contract is set in bbram when EC boot up, then
		 * TCPMv1 set soft reset as first state instead of
		 * unattached.SNK, so we need to enable BMC PHY for tx module.
		 *
		 * NOTE: If the platform is without battery and connects to
		 * adapter, then cold reset EC, our Rd is always asserted on cc,
		 * so adapter keeps providing 5v and data in BBRAM are still
		 * alive.
		 */
		if ((pd_get_saved_port_flags(port, &flags) == EC_SUCCESS) &&
		    (flags & PD_BBRMFLG_EXPLICIT_CONTRACT))
			USBPD_ENABLE_BMC_PHY(port);
	}
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
#ifdef IT83XX_INTC_PLUG_IN_OUT_SUPPORT
	/*
	 * when tcpc detect type-c plug in (cc lines voltage change), it will
	 * interrupt fw to wake pd task, so task can react immediately.
	 *
	 * w/c status and unmask TCDCR (detect type-c plug in interrupt default
	 * is enable).
	 */
	IT83XX_USBPD_TCDCR(port) = USBPD_REG_PLUG_IN_OUT_DETECT_STAT;
#endif
	/* cc connect */
	IT83XX_USBPD_CCCSR(port) = 0;
	/* disable vconn */
	it83xx_tcpm_set_vconn(port, 0);
	/* TX start from high */
	IT83XX_USBPD_CCADCR(port) |= BIT(6);
	/* enable cc1/cc2 */
	*usbpd_ctrl_regs[port].cc1 = IT83XX_USBPD_CC_PIN_CONFIG;
	*usbpd_ctrl_regs[port].cc2 = IT83XX_USBPD_CC_PIN_CONFIG;
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

	/* Keep track of current RP value */
	tcpci_set_cached_rp(port, rp_sel);

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

static int it83xx_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	enum usbpd_cc_pin cc_pin =
		(polarity == POLARITY_CC1 || polarity == POLARITY_CC1_DTS) ?
			USBPD_CC_PIN_1 :
			USBPD_CC_PIN_2;

	it83xx_select_polarity(port, cc_pin);

	return EC_SUCCESS;
}

__maybe_unused static int it83xx_tcpm_decode_sop_prime_enable(int port,
							      bool enable)
{
	/* Save SOP'/SOP'' enable state */
	sop_prime_en[port] = enable;

	if (rx_en[port]) {
		if (enable)
			IT83XX_USBPD_PDMSR(port) |=
				(USBPD_REG_MASK_SOPP_ENABLE |
				 USBPD_REG_MASK_SOPPP_ENABLE);
		else
			IT83XX_USBPD_PDMSR(port) &=
				~(USBPD_REG_MASK_SOPP_ENABLE |
				  USBPD_REG_MASK_SOPPP_ENABLE);
	}

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
				/* Enable tcpc receive SOP' and SOP'' packet */
				it83xx_tcpm_decode_sop_prime_enable(port, true);
			/* Turn on Vconn power switch. */
			board_pd_vconn_ctrl(port,
					    USBPD_GET_PULL_CC_SELECTION(port) ?
						    USBPD_CC_PIN_2 :
						    USBPD_CC_PIN_1,
					    enable);
		} else {
			/*
			 * If the pd port has previous connection and supplies
			 * Vconn, then RO jumping to RW reset the system,
			 * we never know which cc is the previous Vconn pin,
			 * so we always turn both cc pins off when disable
			 * Vconn power switch.
			 */
			board_pd_vconn_ctrl(port, USBPD_CC_PIN_1, enable);
			board_pd_vconn_ctrl(port, USBPD_CC_PIN_2, enable);
			/* Disable tcpc receive SOP' and SOP'' packet */
			if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
				it83xx_tcpm_decode_sop_prime_enable(port,
								    false);
			/*
			 * Before disabling cc 5v tolerant, we need to make
			 * sure cc voltage detector is enabled and Vconn is
			 * dropped below 3.3v (>500us) to avoid the potential
			 * risk of voltage fed back into Vcore.
			 */
			crec_usleep(IT83XX_USBPD_T_VCONN_BELOW_3_3V);
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
	/* PD_ROLE_SINK 0, PD_ROLE_SOURCE 1 */
	if (power_role == PD_ROLE_SOURCE)
		/* bit0: source */
		SET_MASK(IT83XX_USBPD_PDMSR(port), BIT(0));
	else
		/* bit0: sink */
		CLEAR_MASK(IT83XX_USBPD_PDMSR(port), BIT(0));

	it83xx_set_data_role(port, data_role);

	return EC_SUCCESS;
}

static int it83xx_tcpm_set_rx_enable(int port, int enable)
{
	/* Save rx_on */
	rx_en[port] = enable;

	if (enable) {
		IT83XX_USBPD_IMR(port) &= ~USBPD_REG_MASK_MSG_RX_DONE;
		IT83XX_USBPD_PDMSR(port) |= USBPD_REG_MASK_SOP_ENABLE;
		IT83XX_USBPD_VDMMCSR(port) |= USBPD_REG_MASK_HARD_RESET_DECODE;
		if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
			it83xx_tcpm_decode_sop_prime_enable(port,
							    sop_prime_en[port]);
	} else {
		IT83XX_USBPD_IMR(port) |= USBPD_REG_MASK_MSG_RX_DONE;
		IT83XX_USBPD_PDMSR(port) &= ~(USBPD_REG_MASK_SOP_ENABLE |
					      USBPD_REG_MASK_SOPP_ENABLE |
					      USBPD_REG_MASK_SOPPP_ENABLE);
		IT83XX_USBPD_VDMMCSR(port) &= ~USBPD_REG_MASK_HARD_RESET_DECODE;
	}

	return EC_SUCCESS;
}

static int it83xx_tcpm_transmit(int port, enum tcpci_msg_type type,
				uint16_t header, const uint32_t *data)
{
	int status = TCPC_TX_COMPLETE_FAILED;

	switch (type) {
	case TCPCI_MSG_SOP:
	case TCPCI_MSG_SOP_PRIME:
	case TCPCI_MSG_SOP_PRIME_PRIME:
	case TCPCI_MSG_SOP_DEBUG_PRIME:
	case TCPCI_MSG_SOP_DEBUG_PRIME_PRIME:
		status = it83xx_tx_data(port, type, header, data);
		break;
	case TCPCI_MSG_TX_BIST_MODE_2:
		it83xx_send_bist_mode2_pattern(port);
		status = TCPC_TX_COMPLETE_SUCCESS;
		break;
	case TCPCI_MSG_TX_HARD_RESET:
	case TCPCI_MSG_CABLE_RESET:
		status = it83xx_send_hw_reset(port, type);
		break;
	default:
		status = TCPC_TX_COMPLETE_FAILED;
		break;
	}
	pd_transmit_complete(port, status);

	return EC_SUCCESS;
}

static int
it83xx_tcpm_get_chip_info(int port, int live,
			  struct ec_response_pd_chip_info_v1 *chip_info)
{
	chip_info->vendor_id = USB_VID_ITE;
	chip_info->product_id =
		((IT83XX_GCTRL_CHIPID1 << 8) | IT83XX_GCTRL_CHIPID2);
	chip_info->device_id = IT83XX_GCTRL_CHIPVER & 0xf;
	chip_info->fw_version_number = 0xEC;

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int it83xx_tcpm_enter_low_power_mode(int port)
{
	/*
	 * ITE embedded TCPC SLEEP_MASK_USB_PD flag is only controlled by
	 * it83xx driver in tc_update_pd_sleep_mask(), and do low power
	 * mode in idle_task().
	 * In deep sleep mode, ITE TCPC clock is turned off, and the
	 * timer every 5ms to exit the mode and wakeup PD task to run
	 * (ex. change the CC lines termination).
	 */
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
		IT83XX_USBPD_TCDCR(port) =
			(IT83XX_USBPD_TCDCR(port) &
			 ~USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE) |
			USBPD_REG_PLUG_OUT_DETECT_TYPE_SELECT |
			USBPD_REG_PLUG_OUT_SELECT;
	else if (cc1 == TYPEC_CC_VOLT_RD || cc2 == TYPEC_CC_VOLT_RD)
		/* We're source, switch to detect sink plug out. */
		IT83XX_USBPD_TCDCR(port) =
			(IT83XX_USBPD_TCDCR(port) &
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

__override void tc_update_pd_sleep_mask(int port)
{
	int i;
	bool prevent_deep_sleep = false;

	/*
	 * Check ITE embedded pd ports to set SLEEP_MASK_USB_PD:
	 * In tc_attached_[src, snk]_entry states, calling HOOK_CONNECT
	 * to enable BMC PHY, and if one of ITE pd ports BMC PHY is enable,
	 * then EC deep doze mode will be disabled.
	 * Otherwise, EC deep doze mode will be enabled.
	 */
	for (i = 0; i < CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT; ++i) {
		if (IT83XX_USBPD_GCR(i) & USBPD_REG_MASK_BMC_PHY) {
			prevent_deep_sleep = true;
			break;
		}
	}

	/*
	 * Check Other stand alone pd ports to set SLEEP_MASK_USB_PD:
	 * In [pe_src_send_capabilities_run, pe_snk_select_capability_entry]
	 * states receive [GoodCRC, SRC_CAP] from port partner, then EC deep
	 * doze mode will be disabled.
	 * Otherwise, EC deep doze mode will be enabled.
	 */
	if (!prevent_deep_sleep) {
		for (; i < board_get_usb_pd_port_count(); i++)
			if (pd_capable(i))
				prevent_deep_sleep = true;
	}

	if (prevent_deep_sleep)
		disable_sleep(SLEEP_MASK_USB_PD);
	else
		enable_sleep(SLEEP_MASK_USB_PD);
}

static void it83xx_tcpm_hook_connect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * If it isn't ITE active port, then return.
	 *
	 * NOTE: If we don't use all the ITE pd ports on a board, then we
	 *       need to start from port0 to use the ITE pd port. If we
	 *       start from port1, then port1 HOOK function never works.
	 */
	if (port > (CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT - 1))
		return;

#ifdef CONFIG_USB_PD_TCPMV2
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
	 * out or the SNK disable detect, so TCPMv1 needn't this.
	 */
	it83xx_tcpm_switch_plug_out_type(port);
#endif
	/* Enable PD PHY Tx and Rx module since type-c has connected. */
	USBPD_ENABLE_BMC_PHY(port);
	/*
	 * After we're in attached.[SRC, SNK] states and before we receive
	 * [GoodCRC of SRC_CAP, SRC_CAP] this period time, if EC goes to
	 * deep doze mode, the pd clock will be gated, then pd can't
	 * transmit and receive any messages, so we disable deep doze mode
	 * to make sure that pd won't miss any messages.
	 */
	tc_update_pd_sleep_mask(port);
}

DECLARE_HOOK(HOOK_USB_PD_CONNECT, it83xx_tcpm_hook_connect, HOOK_PRIO_DEFAULT);

static void it83xx_tcpm_hook_disconnect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * If it isn't ITE active port, then return.
	 *
	 * NOTE: If we don't use all the ITE pd ports on a board, then we
	 *       need to start from port0 to use the ITE pd port. If we
	 *       start from port1, then port1 HOOK function never works.
	 */
	if (port > (CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT - 1))
		return;

	if (IS_ENABLED(IT83XX_INTC_PLUG_IN_OUT_SUPPORT))
		/*
		 * Switch to detect plug in and enable detect plug in interrupt,
		 * since pd task has detected a type-c physical disconnected.
		 */
		IT83XX_USBPD_TCDCR(port) &=
			~(USBPD_REG_PLUG_OUT_SELECT |
			  USBPD_REG_PLUG_IN_OUT_DETECT_DISABLE);

	/* exit BIST test data mode */
	USBPD_SW_RESET(port);

	/*
	 * Init rx status and disable PD PHY Tx and Rx module for better power
	 * consumption since type-c has disconnected.
	 */
	rx_en[port] = 0;
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		sop_prime_en[port] = 0;
	USBPD_DISABLE_BMC_PHY(port);
	/*
	 * Since PD BMC PHY is off, then EC can go to deep doze mode and
	 * turn off pd clock.
	 */
	tc_update_pd_sleep_mask(port);
}

DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, it83xx_tcpm_hook_disconnect,
	     HOOK_PRIO_DEFAULT);

const struct tcpm_drv it83xx_tcpm_drv = {
	.init = &it83xx_tcpm_init,
	.release = &it83xx_tcpm_release,
	.get_cc = &it83xx_tcpm_get_cc,
	.select_rp_value = &it83xx_tcpm_select_rp_value,
	.set_cc = &it83xx_tcpm_set_cc,
	.set_polarity = &it83xx_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &it83xx_tcpm_decode_sop_prime_enable,
#endif
	.set_vconn = &it83xx_tcpm_set_vconn,
	.set_msg_header = &it83xx_tcpm_set_msg_header,
	.set_rx_enable = &it83xx_tcpm_set_rx_enable,
	.get_message_raw = &it83xx_tcpm_get_message_raw,
	.transmit = &it83xx_tcpm_transmit,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = NULL,
#endif
	.get_chip_info = &it83xx_tcpm_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &it83xx_tcpm_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable = &it83xx_tcpm_set_frs_enable,
#endif
};
