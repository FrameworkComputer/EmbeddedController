/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Author : Analogix Semiconductor.
 */

/* Type-C port manager for Analogix's anx74xx chips */

#include "anx74xx.h"
#include "console.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#if !defined(CONFIG_USB_PD_TCPM_TCPCI)
#error "ANX74xx is using part of standard TCPCI control"
#error "Please upgrade your board configuration"
#endif

#if defined(CONFIG_USB_PD_REV30)
#error "ANX74xx chips were developed before PD 3.0 and aren't PD 3.0 compliant"
#error "Please undefine PD 3.0.  See b/159253723 for details"
#endif

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

struct anx_state {
	int polarity;
	int vconn_en;
	int mux_state;
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	int prev_mode;
#endif
};
#define clear_recvd_msg_int(port)                                      \
	do {                                                           \
		int reg, rv;                                           \
		rv = tcpc_read(port, ANX74XX_REG_RECVD_MSG_INT, &reg); \
		if (!rv)                                               \
			tcpc_write(port, ANX74XX_REG_RECVD_MSG_INT,    \
				   reg | 0x01);                        \
	} while (0)

static struct anx_state anx[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_USB_PD_DECODE_SOP
/* Save the message address */
static int msg_sop[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif

static int anx74xx_tcpm_init(int port);

static void anx74xx_tcpm_set_auto_good_crc(int port, int enable)
{
	int reply_sop_en = 0;

	if (enable) {
		reply_sop_en = ANX74XX_REG_REPLY_SOP_EN;
#ifdef CONFIG_USB_PD_DECODE_SOP
		/*
		 * Only the VCONN Source is allowed to communicate
		 * with the Cable Plugs.
		 */
		if (anx[port].vconn_en) {
			reply_sop_en |= ANX74XX_REG_REPLY_SOP_1_EN |
					ANX74XX_REG_REPLY_SOP_2_EN;
		}
#endif
	}

	tcpc_write(port, ANX74XX_REG_TX_AUTO_GOODCRC_2, reply_sop_en);
}

static void anx74xx_update_cable_det(int port, int mode)
{
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	int reg;

	if (anx[port].prev_mode == mode)
		return;

	/* Update power mode */
	anx[port].prev_mode = mode;

	/* Get ANALOG_CTRL_0 for cable det bit */
	if (tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_0, &reg))
		return;

	if (mode == ANX74XX_STANDBY_MODE) {
		int cc_reg;

		/*
		 * The ANX4329 enters standby mode by setting PWR_EN signal
		 * low. In addition, RESET_L must be set low to keep the ANX3429
		 * in standby mode.
		 *
		 * Clearing bit 7 of ANX74XX_REG_ANALOG_CTRL_0 will cause the
		 * ANX3429 to clear the cable_det signal that goes from the
		 * ANX3429 to the EC. If this bit is cleared when a cable is
		 * attached then cable_det will go high once standby is entered.
		 *
		 * In some cases, such as when the chipset power state is
		 * S3/S5/G3 and a sink only adapter is connected to the port,
		 * this behavior is undesirable. The constant toggling between
		 * standby and normal mode means that effectively the ANX3429 is
		 * not in standby mode only consumes ~1 mW less than just
		 * remaining in normal mode. However when an E mark cable is
		 * connected, clearing bit 7 is required so that while the E
		 * mark cable configuration happens, the USB PD state machine
		 * will continue to wake up until the USB PD attach event can be
		 * regtistered.
		 *
		 * Therefore, the decision to clear bit 7 is based on the
		 * current CC status of the port. If the CC status is open for
		 * both CC lines OR if either CC line is showing Ra, then clear
		 * bit 7. Not clearing bit 7 has no impact for normal cables and
		 * prevents the constant toggle of standby<->normal when an
		 * adapter is connected that isn't allowed to attach. Clearing
		 * bit 7 when CC status reads Ra for either CC line allows the
		 * USB PD state machine to be woken until the attach event can
		 * happen. Note that in the case an E mark cable is connected
		 * and can't attach (i.e. sink only port <- Emark cable -> sink
		 * only adapter), then the ANX3429 will toggle indefinitely,
		 * until either the cable is removed, or the port drp status
		 * changes so the attach event can occur.
		 * .
		 */

		/* Read CC status to see if cable_det bit should be cleared */
		if (tcpc_read(port, ANX74XX_REG_CC_STATUS, &cc_reg))
			return;
		/* If open or either CC line is Ra, then clear cable_det */
		if (!cc_reg || (cc_reg & ANX74XX_CC_RA_MASK &&
				!(cc_reg & ANX74XX_CC_RD_MASK)))
			reg &= ~ANX74XX_REG_R_PIN_CABLE_DET;
	} else {
		reg |= ANX74XX_REG_R_PIN_CABLE_DET;
	}

	tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_0, reg);
#endif
}

static void anx74xx_set_power_mode(int port, int mode)
{
	/*
	 * Update PWR_EN and RESET_N signals to the correct level. High for
	 * Normal mode and low for Standby mode. When transitioning from standby
	 * to normal mode, must set the PWR_EN and RESET_N before attempting to
	 * modify cable_det bit of analog_ctrl_0. If going from Normal to
	 * Standby, updating analog_ctrl_0 must happen before setting PWR_EN and
	 * RESET_N low.
	 */
	if (mode == ANX74XX_NORMAL_MODE) {
		/* Take chip out of standby mode */
		board_set_tcpc_power_mode(port, mode);
		/* Update the cable det signal */
		anx74xx_update_cable_det(port, mode);
	} else {
		/* Update cable cable det signal */
		anx74xx_update_cable_det(port, mode);
		/*
		 * Delay between setting cable_det low and setting RESET_L low
		 * as recommended the ANX3429 datasheet.
		 */
		msleep(1);
		/* Put chip into standby mode */
		board_set_tcpc_power_mode(port, mode);
	}
}

#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) && \
	defined(CONFIG_USB_PD_TCPC_LOW_POWER)

static int anx74xx_tcpc_drp_toggle(int port)
{
	/*
	 * The ANX3429 always auto-toggles when in low power mode. Since this is
	 * not configurable, there is nothing to do here. DRP auto-toggle will
	 * happen once low power mode is set via anx74xx_enter_low_power_mode().
	 * Note: this means the ANX3429 auto-toggles in PD_DRP_FORCE_SINK mode,
	 * which is undesirable (b/72007056).
	 */
	return EC_SUCCESS;
}

static int anx74xx_enter_low_power_mode(int port)
{
	anx74xx_set_power_mode(port, ANX74XX_STANDBY_MODE);
	return EC_SUCCESS;
}

#endif

void anx74xx_tcpc_set_vbus(int port, int enable)
{
	int reg;

	tcpc_read(port, ANX74XX_REG_GPIO_CTRL_4_5, &reg);
	if (enable)
		reg |= ANX74XX_REG_SET_VBUS;
	else
		reg &= ~ANX74XX_REG_SET_VBUS;
	tcpc_write(port, ANX74XX_REG_GPIO_CTRL_4_5, reg);
}

#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
static void anx74xx_tcpc_discharge_vbus(int port, int enable)
{
	int reg;

	tcpc_read(port, ANX74XX_REG_HPD_CTRL_0, &reg);
	if (enable)
		reg |= ANX74XX_REG_DISCHARGE_CTRL;
	else
		reg &= ~ANX74XX_REG_DISCHARGE_CTRL;
	tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);
}
#endif

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

void anx74xx_tcpc_update_hpd_status(const struct usb_mux *me,
				    mux_state_t mux_state, bool *ack_required)
{
	int reg;
	int port = me->usb_port;
	int hpd_lvl = (mux_state & USB_PD_MUX_HPD_LVL) ? 1 : 0;
	int hpd_irq = (mux_state & USB_PD_MUX_HPD_IRQ) ? 1 : 0;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	mux_read(me, ANX74XX_REG_HPD_CTRL_0, &reg);
	if (hpd_lvl)
		reg |= ANX74XX_REG_HPD_OUT_DATA;
	else
		reg &= ~ANX74XX_REG_HPD_OUT_DATA;
	mux_write(me, ANX74XX_REG_HPD_CTRL_0, reg);

	if (hpd_irq) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < hpd_deadline[port])
			usleep(hpd_deadline[port] - now);

		mux_read(me, ANX74XX_REG_HPD_CTRL_0, &reg);
		reg &= ~ANX74XX_REG_HPD_OUT_DATA;
		mux_write(me, ANX74XX_REG_HPD_CTRL_0, reg);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		reg |= ANX74XX_REG_HPD_OUT_DATA;
		mux_write(me, ANX74XX_REG_HPD_CTRL_0, reg);
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

void anx74xx_tcpc_clear_hpd_status(int port)
{
	int reg;

	tcpc_read(port, ANX74XX_REG_HPD_CTRL_0, &reg);
	reg &= 0xcf;
	tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);
}

#ifdef CONFIG_USB_PD_TCPM_MUX
static int anx74xx_tcpm_mux_init(const struct usb_mux *me)
{
	/* Nothing to do here, ANX initializes its muxes
	 * as (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED)
	 */
	anx[me->usb_port].mux_state = USB_PD_MUX_USB_ENABLED |
				      USB_PD_MUX_DP_ENABLED;

	return EC_SUCCESS;
}

static int anx74xx_tcpm_mux_enter_safe_mode(int port)
{
	int reg;
	const struct usb_mux *me = usb_muxes[port].mux;

	if (mux_read(me, ANX74XX_REG_ANALOG_CTRL_2, &reg))
		return EC_ERROR_UNKNOWN;
	if (mux_write(me, ANX74XX_REG_ANALOG_CTRL_2,
		      reg | ANX74XX_REG_MODE_TRANS))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int anx74xx_tcpm_mux_exit_safe_mode(int port)
{
	int reg;
	const struct usb_mux *me = usb_muxes[port].mux;

	if (mux_read(me, ANX74XX_REG_ANALOG_CTRL_2, &reg))
		return EC_ERROR_UNKNOWN;
	if (mux_write(me, ANX74XX_REG_ANALOG_CTRL_2,
		      reg & ~ANX74XX_REG_MODE_TRANS))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int anx74xx_tcpm_mux_exit(int port)
{
	int reg;
	const struct usb_mux *me = usb_muxes[port].mux;

	/*
	 * Safe mode must be entered before any changes are made to the mux
	 * settings used to enable ALT_DP mode. This function is called either
	 * from anx74xx_tcpm_mux_set when USB_PD_MUX_NONE is selected as the
	 * new mux state, or when both cc lines are determined to be
	 * TYPEC_CC_VOLT_OPEN. Therefore, safe mode must be entered and exited
	 * here so that both entry paths are handled.
	 */
	if (anx74xx_tcpm_mux_enter_safe_mode(port))
		return EC_ERROR_UNKNOWN;

	/* Disconnect aux from sbu */
	if (mux_read(me, ANX74XX_REG_ANALOG_CTRL_2, &reg))
		return EC_ERROR_UNKNOWN;
	if (mux_write(me, ANX74XX_REG_ANALOG_CTRL_2, reg & 0xf))
		return EC_ERROR_UNKNOWN;

	/* Clear Bit[7:0] R_SWITCH */
	if (mux_write(me, ANX74XX_REG_ANALOG_CTRL_1, 0x0))
		return EC_ERROR_UNKNOWN;
	/* Clear Bit[7:4] R_SWITCH_H */
	if (mux_read(me, ANX74XX_REG_ANALOG_CTRL_5, &reg))
		return EC_ERROR_UNKNOWN;
	if (mux_write(me, ANX74XX_REG_ANALOG_CTRL_5, reg & 0x0f))
		return EC_ERROR_UNKNOWN;

	/* Exit safe mode */
	if (anx74xx_tcpm_mux_exit_safe_mode(port))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int anx74xx_mux_aux_to_sbu(int port, int polarity, int enabled)
{
	int reg;
	const int aux_mask = ANX74XX_REG_AUX_SWAP_SET_CC2 |
			     ANX74XX_REG_AUX_SWAP_SET_CC1;
	const struct usb_mux *me = usb_muxes[port].mux;

	/*
	 * Get the current value of analog_ctrl_2 register. Note, that safe mode
	 * is enabled and exited by the calling function, so only have to worry
	 * about setting the correct value for the upper 4 bits of analog_ctrl_2
	 * here.
	 */
	if (mux_read(me, ANX74XX_REG_ANALOG_CTRL_2, &reg))
		return EC_ERROR_UNKNOWN;

	/* Assume aux_p/n lines are not connected */
	reg &= ~aux_mask;

	if (enabled) {
		/* If enabled, connect aux to sbu based on desired  polarity */
		if (polarity)
			reg |= ANX74XX_REG_AUX_SWAP_SET_CC2;
		else
			reg |= ANX74XX_REG_AUX_SWAP_SET_CC1;
	}
	/* Write new aux <-> sbu settings */
	if (mux_write(me, ANX74XX_REG_ANALOG_CTRL_2, reg))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int anx74xx_tcpm_mux_set(const struct usb_mux *me, mux_state_t mux_state,
				bool *ack_required)
{
	int ctrl5;
	int ctrl1 = 0;
	int rv;
	int port = me->usb_port;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	if (!(mux_state & ~USB_PD_MUX_POLARITY_INVERTED)) {
		anx[port].mux_state = mux_state;
		return anx74xx_tcpm_mux_exit(port);
	}

	rv = mux_read(me, ANX74XX_REG_ANALOG_CTRL_5, &ctrl5);
	if (rv)
		return EC_ERROR_UNKNOWN;
	ctrl5 &= 0x0f;

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Connect USB SS switches */
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED) {
			ctrl1 = ANX74XX_REG_MUX_SSRX_RX2;
			ctrl5 |= ANX74XX_REG_MUX_SSTX_TX2;
		} else {
			ctrl1 = ANX74XX_REG_MUX_SSRX_RX1;
			ctrl5 |= ANX74XX_REG_MUX_SSTX_TX1;
		}
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			/* Set pin assignment D */
			if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
				ctrl1 |= (ANX74XX_REG_MUX_ML0_RX1 |
					  ANX74XX_REG_MUX_ML1_TX1);
			else
				ctrl1 |= (ANX74XX_REG_MUX_ML0_RX2 |
					  ANX74XX_REG_MUX_ML1_TX2);
		}
		/* Keep ML0/ML1 unconnected if DP is not enabled */
	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Set pin assignment C */
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED) {
			ctrl1 = (ANX74XX_REG_MUX_ML0_RX1 |
				 ANX74XX_REG_MUX_ML1_TX1 |
				 ANX74XX_REG_MUX_ML3_RX2);
			ctrl5 |= ANX74XX_REG_MUX_ML2_TX2;
		} else {
			ctrl1 = (ANX74XX_REG_MUX_ML0_RX2 |
				 ANX74XX_REG_MUX_ML1_TX2 |
				 ANX74XX_REG_MUX_ML3_RX1);
			ctrl5 |= ANX74XX_REG_MUX_ML2_TX1;
		}
	} else if (!mux_state) {
		return anx74xx_tcpm_mux_exit(port);
	} else {
		return EC_ERROR_UNIMPLEMENTED;
	}

	/*
	 * Safe mode must be entererd prior to any changes to the mux related to
	 * ALT_DP mode. Therefore, first enable safe mode prior to updating the
	 * values for analog_ctrl_1, analog_ctrl_5, and analog_ctrl_2.
	 */
	if (anx74xx_tcpm_mux_enter_safe_mode(port))
		return EC_ERROR_UNKNOWN;

	/* Write updated pin assignment */
	rv = mux_write(me, ANX74XX_REG_ANALOG_CTRL_1, ctrl1);
	/* Write Rswitch config bits */
	rv |= mux_write(me, ANX74XX_REG_ANALOG_CTRL_5, ctrl5);
	if (rv)
		return EC_ERROR_UNKNOWN;

	/* Configure DP aux to sbu settings */
	if (anx74xx_mux_aux_to_sbu(port,
				   mux_state & USB_PD_MUX_POLARITY_INVERTED,
				   mux_state & USB_PD_MUX_DP_ENABLED))
		return EC_ERROR_UNKNOWN;

	/* Exit safe mode */
	if (anx74xx_tcpm_mux_exit_safe_mode(port))
		return EC_ERROR_UNKNOWN;

	anx[port].mux_state = mux_state;

	return EC_SUCCESS;
}

/* current mux state  */
static int anx74xx_tcpm_mux_get(const struct usb_mux *me,
				mux_state_t *mux_state)
{
	*mux_state = anx[me->usb_port].mux_state;

	return EC_SUCCESS;
}

const struct usb_mux_driver anx74xx_tcpm_usb_mux_driver = {
	.init = &anx74xx_tcpm_mux_init,
	.set = &anx74xx_tcpm_mux_set,
	.get = &anx74xx_tcpm_mux_get,
};
#endif /* CONFIG_USB_PD_TCPM_MUX */

static int anx74xx_init_analog(int port)
{
	int reg, rv = EC_SUCCESS;

	/* Analog settings for chip */
	rv |= tcpc_write(port, ANX74XX_REG_HPD_CONTROL,
			 ANX74XX_REG_HPD_OP_MODE);
	rv |= tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, ANX74XX_REG_HPD_DEFAULT);
	if (rv)
		return rv;
	rv = tcpc_read(port, ANX74XX_REG_GPIO_CTRL_4_5, &reg);
	if (rv)
		return rv;
	reg &= ANX74XX_REG_VBUS_GPIO_MODE;
	reg |= ANX74XX_REG_VBUS_OP_ENABLE;
	rv = tcpc_write(port, ANX74XX_REG_GPIO_CTRL_4_5, reg);
	if (rv)
		return rv;
	rv = tcpc_read(port, ANX74XX_REG_CC_SOFTWARE_CTRL, &reg);
	if (rv)
		return rv;
	reg |= ANX74XX_REG_TX_MODE_ENABLE;
	rv = tcpc_write(port, ANX74XX_REG_CC_SOFTWARE_CTRL, reg);

	return rv;
}

static int anx74xx_send_message(int port, uint16_t header,
				const uint32_t *payload, int type, uint8_t len)
{
	int reg, rv = EC_SUCCESS;
	uint8_t *buf = NULL;
	int num_retry = 0, i = 0;
	/* If sending Soft_reset, clear received message */
	/* Soft Reset Message type = 1101 and Number of Data Object = 0 */
	if ((header & 0x700f) == 0x000d) {
		/*
		 * When sending soft reset,
		 * the Rx buffer of ANX3429 shall be clear
		 */
		rv = tcpc_read(port, ANX74XX_REG_CTRL_FW, &reg);
		rv |= tcpc_write(port, ANX74XX_REG_CTRL_FW,
				 reg | CLEAR_RX_BUFFER);
		if (rv)
			return EC_ERROR_UNKNOWN;
		tcpc_write(port, ANX74XX_REG_RECVD_MSG_INT, 0xFF);
	}
	/* Inform chip about message length and TX type
	 * type->bit-0..2, len->bit-3..7
	 */
	reg = (len << 3) & 0xf8;
	reg |= type & 0x07;
	rv |= tcpc_write(port, ANX74XX_REG_TX_CTRL_2, reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	/* Enqueue Header */
	rv = tcpc_write(port, ANX74XX_REG_TX_HEADER_L, (header & 0xff));
	if (rv)
		return EC_ERROR_UNKNOWN;
	rv = tcpc_write(port, ANX74XX_REG_TX_HEADER_H, (header >> 8));
	if (rv)
		return EC_ERROR_UNKNOWN;
	/* Enqueue payload */
	if (len > 2) {
		len -= 2;
		buf = (uint8_t *)payload;
		while (1) {
			if (i < 18)
				rv = tcpc_write(port,
						ANX74XX_REG_TX_START_ADDR_0 + i,
						*buf);
			else
				rv = tcpc_write(port,
						ANX74XX_REG_TX_START_ADDR_1 +
							i - 18,
						*buf);
			if (rv) {
				num_retry++;
			} else {
				buf++;
				len--;
				num_retry = 0;
				i++;
			}
			if (len == 0 || num_retry >= 3)
				break;
		}
		/* If enqueue failed, do not request anx to transmit
		 * messages, FIFO will get cleared in next call
		 * before enqueue.
		 * num_retry = 0, refer to success
		 */
		if (num_retry)
			return EC_ERROR_UNKNOWN;
	}
	/* Request a data transmission
	 * This bit will be cleared by ANX after TX success
	 */
	rv = tcpc_read(port, ANX74XX_REG_CTRL_COMMAND, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	reg |= ANX74XX_REG_TX_SEND_DATA_REQ;
	rv |= tcpc_write(port, ANX74XX_REG_CTRL_COMMAND, reg);

	return rv;
}

static int anx74xx_read_pd_obj(int port, uint8_t *buf, int plen)
{
	int rv = EC_SUCCESS, i;
	int reg, addr = ANX74XX_REG_PD_RX_DATA_OBJ;

	/* Read PD data objects from ANX */
	for (i = 0; i < plen; i++) {
		/* Register sequence changes for last two bytes, if
		 * plen is greater than 26
		 */
		if (i == 26)
			addr = ANX74XX_REG_PD_RX_DATA_OBJ_M;
		rv = tcpc_read(port, addr + i, &reg);
		if (rv)
			break;
		buf[i] = reg;
	}
	clear_recvd_msg_int(port);
	return rv;
}

static int anx74xx_check_cc_type(int cc_reg)
{
	int cc;

	switch (cc_reg & ANX74XX_REG_CC_STATUS_MASK) {
	case BIT_VALUE_OF_SRC_CC_RD:
		cc = TYPEC_CC_VOLT_RD;
		break;

	case BIT_VALUE_OF_SRC_CC_RA:
		cc = TYPEC_CC_VOLT_RA;
		break;

	case BIT_VALUE_OF_SNK_CC_DEFAULT:
		cc = TYPEC_CC_VOLT_RP_DEF;
		break;

	case BIT_VALUE_OF_SNK_CC_1_P_5:
		cc = TYPEC_CC_VOLT_RP_1_5;
		break;

	case BIT_VALUE_OF_SNK_CC_3_P_0:
		cc = TYPEC_CC_VOLT_RP_3_0;
		break;

	default:
		/* If no bits are set, then nothing is attached */
		cc = TYPEC_CC_VOLT_OPEN;
	}

	return cc;
}

static int anx74xx_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
			       enum tcpc_cc_voltage_status *cc2)
{
	__maybe_unused int rv = EC_SUCCESS;
	int reg = 0;

	/* Read tcpc cc status register */
	rv |= tcpc_read(port, ANX74XX_REG_CC_STATUS, &reg);
	/* Check for cc1 type */
	*cc1 = anx74xx_check_cc_type(reg);
	/*
	 * Check for cc2 type (note cc2 bits are upper 4 of cc status
	 * register.
	 */
	*cc2 = anx74xx_check_cc_type(reg >> 4);

	/* clear HPD status*/
	if (!(*cc1) && !(*cc2)) {
		anx74xx_tcpc_clear_hpd_status(port);
#ifdef CONFIG_USB_PD_TCPM_MUX
		anx74xx_tcpm_mux_exit(port);
#endif
	}

	return EC_SUCCESS;
}

static int anx74xx_rp_control(int port, int rp)
{
	int reg;
	int rv;

	rv = tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_6, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	/* clear Bit[0,1] R_RP to default Rp's value */
	reg &= ~0x03;

	switch (rp) {
	case TYPEC_RP_1A5:
		/* Set Rp strength to 12K for presenting 1.5A */
		reg |= ANX74XX_REG_CC_PULL_RP_12K;
		break;
	case TYPEC_RP_3A0:
		/* Set Rp strength to 4K for presenting 3A */
		reg |= ANX74XX_REG_CC_PULL_RP_4K;
		break;
	case TYPEC_RP_USB:
	default:
		/* default: Set Rp strength to 36K */
		break;
	}

	return tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_6, reg);
}

static int anx74xx_tcpm_select_rp_value(int port, int rp)
{
	/* Keep track of current RP value */
	tcpci_set_cached_rp(port, rp);

	/* For ANX3429 cannot get cc correctly when Rp != USB_Default */
	return EC_SUCCESS;
}

static int anx74xx_cc_software_ctrl(int port, int enable)
{
	int rv;
	int reg;

	rv = tcpc_read(port, ANX74XX_REG_CC_SOFTWARE_CTRL, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	if (enable)
		reg |= ANX74XX_REG_CC_SW_CTRL_ENABLE;
	else
		reg &= ~ANX74XX_REG_CC_SW_CTRL_ENABLE;

	rv |= tcpc_write(port, ANX74XX_REG_CC_SOFTWARE_CTRL, reg);
	return rv;
}

static int anx74xx_tcpm_set_cc(int port, int pull)
{
	int rv = EC_SUCCESS;
	int reg;

	/* Enable CC software Control */
	rv = anx74xx_cc_software_ctrl(port, 1);
	if (rv)
		return EC_ERROR_UNKNOWN;

	switch (pull) {
	case TYPEC_CC_RP:
		/* Enable Rp */
		rv |= tcpc_read(port, ANX74XX_REG_ANALOG_STATUS, &reg);
		if (rv)
			return EC_ERROR_UNKNOWN;
		reg |= ANX74XX_REG_CC_PULL_RP;
		rv |= tcpc_write(port, ANX74XX_REG_ANALOG_STATUS, reg);
		break;
	case TYPEC_CC_RD:
		/* Enable Rd */
		rv |= tcpc_read(port, ANX74XX_REG_ANALOG_STATUS, &reg);
		if (rv)
			return EC_ERROR_UNKNOWN;
		reg &= ANX74XX_REG_CC_PULL_RD;
		rv |= tcpc_write(port, ANX74XX_REG_ANALOG_STATUS, reg);
		break;
	default:
		rv = EC_ERROR_UNKNOWN;
		break;
	}

	return rv;
}

static int anx74xx_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	int reg, mux_state, rv = EC_SUCCESS;
	const struct usb_mux *me = usb_muxes[port].mux;
	bool unused;

	rv |= tcpc_read(port, ANX74XX_REG_CC_SOFTWARE_CTRL, &reg);
	if (polarity_rm_dts(polarity)) /* Inform ANX to use CC2 */
		reg &= ~ANX74XX_REG_SELECT_CC1;
	else /* Inform ANX to use CC1 */
		reg |= ANX74XX_REG_SELECT_CC1;
	rv |= tcpc_write(port, ANX74XX_REG_CC_SOFTWARE_CTRL, reg);

	anx[port].polarity = polarity;

	/* Update mux polarity */
#ifdef CONFIG_USB_PD_TCPM_MUX
	mux_state = anx[port].mux_state & ~USB_PD_MUX_POLARITY_INVERTED;
	if (polarity_rm_dts(polarity))
		mux_state |= USB_PD_MUX_POLARITY_INVERTED;
	anx74xx_tcpm_mux_set(me, mux_state, &unused);
#endif
	return rv;
}

static int anx74xx_tcpm_set_vconn(int port, int enable)
{
	int reg, rv = EC_SUCCESS;

	/* switch VCONN to Non CC line */
	rv |= tcpc_read(port, ANX74XX_REG_INTP_VCONN_CTRL, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	if (enable) {
		if (anx[port].polarity)
			reg |= ANX74XX_REG_VCONN_1_ENABLE;
		else
			reg |= ANX74XX_REG_VCONN_2_ENABLE;
	} else {
		reg &= ANX74XX_REG_VCONN_DISABLE;
	}
	rv |= tcpc_write(port, ANX74XX_REG_INTP_VCONN_CTRL, reg);
	anx[port].vconn_en = enable;

#ifdef CONFIG_USB_PD_DECODE_SOP
	rv |= tcpc_read(port, ANX74XX_REG_TX_AUTO_GOODCRC_2, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	if (reg & ANX74XX_REG_REPLY_SOP_EN) {
		if (enable) {
			reg |= ANX74XX_REG_REPLY_SOP_1_EN |
			       ANX74XX_REG_REPLY_SOP_2_EN;
		} else {
			reg &= ~(ANX74XX_REG_REPLY_SOP_1_EN |
				 ANX74XX_REG_REPLY_SOP_2_EN);
		}

		tcpc_write(port, ANX74XX_REG_TX_AUTO_GOODCRC_2, reg);
	}
#endif
	return rv;
}

static int anx74xx_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_write(port, ANX74XX_REG_TX_AUTO_GOODCRC_1,
			  ANX74XX_REG_AUTO_GOODCRC_SET(!!data_role,
						       !!power_role));
}

static int anx74xx_tcpm_set_rx_enable(int port, int enable)
{
	int reg, rv;

	rv = tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG_MASK, &reg);
	if (rv)
		return rv;
	if (enable) {
		reg &= ~(ANX74XX_REG_IRQ_CC_MSG_INT);
		anx74xx_tcpm_set_auto_good_crc(port, 1);
		anx74xx_rp_control(port, tcpci_get_cached_rp(port));
	} else {
		/* Disable RX message by masking interrupt */
		reg |= (ANX74XX_REG_IRQ_CC_MSG_INT);
		anx74xx_tcpm_set_auto_good_crc(port, 0);
		anx74xx_rp_control(port, TYPEC_RP_USB);
	}
	/*When this function was call, the interrupt status shall be cleared*/
	tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, 0);

	return tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG_MASK, reg);
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
static bool anx74xx_tcpm_check_vbus_level(int port, enum vbus_level level)
{
	int reg = 0;

	tcpc_read(port, ANX74XX_REG_ANALOG_STATUS, &reg);
	if (level == VBUS_PRESENT)
		return ((reg & ANX74XX_REG_VBUS_STATUS) ? 1 : 0);
	else
		return ((reg & ANX74XX_REG_VBUS_STATUS) ? 0 : 1);
}
#endif

static int anx74xx_tcpm_get_message_raw(int port, uint32_t *payload, int *head)
{
	int reg;
	int len;

	/* Fetch the header */
	if (tcpc_read16(port, ANX74XX_REG_PD_HEADER, &reg)) {
		clear_recvd_msg_int(port);
		return EC_ERROR_UNKNOWN;
	}
	*head = reg;
#ifdef CONFIG_USB_PD_DECODE_SOP
	*head |= PD_HEADER_SOP(msg_sop[port]);
#endif

	len = PD_HEADER_CNT(*head) * 4;
	if (!len) {
		clear_recvd_msg_int(port);
		return EC_SUCCESS;
	}

	/* Receive message : assuming payload have enough
	 * memory allocated
	 */
	return anx74xx_read_pd_obj(port, (uint8_t *)payload, len);
}

static int anx74xx_tcpm_transmit(int port, enum tcpci_msg_type type,
				 uint16_t header, const uint32_t *data)
{
	uint8_t len = 0;
	int ret = 0, reg = 0;

	switch (type) {
	/* ANX is aware of type */
	case TCPCI_MSG_SOP:
	case TCPCI_MSG_SOP_PRIME:
	case TCPCI_MSG_SOP_PRIME_PRIME:
		len = PD_HEADER_CNT(header) * 4 + 2;
		ret = anx74xx_send_message(port, header, data, type, len);
		break;
	case TCPCI_MSG_TX_HARD_RESET:
		/* Request HARD RESET */
		tcpc_read(port, ANX74XX_REG_TX_CTRL_1, &reg);
		reg |= ANX74XX_REG_TX_HARD_RESET_REQ;
		ret = tcpc_write(port, ANX74XX_REG_TX_CTRL_1, reg);
		/*After Hard Reset, TCPM shall disable goodCRC*/
		anx74xx_tcpm_set_auto_good_crc(port, 0);
		break;
	case TCPCI_MSG_CABLE_RESET:
		/* Request CABLE RESET */
		tcpc_read(port, ANX74XX_REG_TX_CTRL_1, &reg);
		reg |= ANX74XX_REG_TX_CABLE_RESET_REQ;
		ret = tcpc_write(port, ANX74XX_REG_TX_CTRL_1, reg);
		break;
	case TCPCI_MSG_TX_BIST_MODE_2:
		/* Request BIST MODE 2 */
		reg = ANX74XX_REG_TX_BIST_START | ANX74XX_REG_TX_BIXT_FOREVER |
		      (0x02 << 4);
		ret = tcpc_write(port, ANX74XX_REG_TX_BIST_CTRL, reg);
		msleep(1);
		ret = tcpc_write(port, ANX74XX_REG_TX_BIST_CTRL,
				 reg | ANX74XX_REG_TX_BIST_ENABLE);
		msleep(30);
		tcpc_read(port, ANX74XX_REG_TX_BIST_CTRL, &reg);
		ret = tcpc_write(port, ANX74XX_REG_TX_BIST_CTRL,
				 reg | ANX74XX_REG_TX_BIST_STOP);
		ret = tcpc_write(port, ANX74XX_REG_TX_BIST_CTRL,
				 reg & (~ANX74XX_REG_TX_BIST_STOP));
		ret = tcpc_write(port, ANX74XX_REG_TX_BIST_CTRL, 0);
		break;
	default:
		return EC_ERROR_UNIMPLEMENTED;
	}

	return ret;
}

/*
 * Don't let the TCPC try to pull from the RX buffer forever. We typical only
 * have 1 or 2 messages waiting.
 */
#define MAX_ALLOW_FAILED_RX_READS 10

void anx74xx_tcpc_alert(int port)
{
	int reg;
	int failed_attempts;

	/* Clear soft irq bit */
	tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_3,
		   ANX74XX_REG_CLEAR_SOFT_IRQ);

	/* Read main alert register for pending alerts */
	reg = 0;
	tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg);

	/* Prioritize TX completion because PD state machine is waiting */
	if (reg & ANX74XX_REG_IRQ_GOOD_CRC_INT)
		pd_transmit_complete(port, TCPC_TX_COMPLETE_SUCCESS);

	if (reg & ANX74XX_REG_IRQ_TX_FAIL_INT)
		pd_transmit_complete(port, TCPC_TX_COMPLETE_FAILED);

	/* Pull all RX messages from TCPC into EC memory */
	failed_attempts = 0;
	while (reg & ANX74XX_REG_IRQ_CC_MSG_INT) {
		if (tcpm_enqueue_message(port))
			++failed_attempts;
		if (tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg))
			++failed_attempts;

		/* Ensure we don't loop endlessly */
		if (failed_attempts >= MAX_ALLOW_FAILED_RX_READS) {
			CPRINTF("C%d Cannot consume RX buffer after %d failed "
				"attempts!",
				port, failed_attempts);
			/*
			 * The port is in a bad state, we don't want to consume
			 * all EC resources so suspend the port for a little
			 * while.
			 */
			pd_set_suspend(port, 1);
			pd_deferred_resume(port);
			return;
		}
	}

	/* Clear all pending alerts */
	tcpc_write(port, ANX74XX_REG_RECVD_MSG_INT, reg);

	if (reg & ANX74XX_REG_IRQ_CC_STATUS_INT)
		/* CC status changed, wake task */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC);

	/* Read and clear extended alert register 1 */
	reg = 0;
	tcpc_read(port, ANX74XX_REG_IRQ_EXT_SOURCE_1, &reg);
	tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_1, reg);

#ifdef CONFIG_USB_PD_DECODE_SOP
	if (reg & ANX74XX_REG_EXT_SOP)
		msg_sop[port] = TCPCI_MSG_SOP;
	else if (reg & ANX74XX_REG_EXT_SOP_PRIME)
		msg_sop[port] = TCPCI_MSG_SOP_PRIME;
#endif

	/* Check for Hard Reset done bit */
	if (reg & ANX74XX_REG_ALERT_TX_HARD_RESETOK)
		/* ANX hardware clears the request bit */
		pd_transmit_complete(port, TCPC_TX_COMPLETE_SUCCESS);

	/* Read and clear TCPC extended alert register 2 */
	reg = 0;
	tcpc_read(port, ANX74XX_REG_IRQ_EXT_SOURCE_2, &reg);
	tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_2, reg);

#ifdef CONFIG_USB_PD_DECODE_SOP
	if (reg & ANX74XX_REG_EXT_SOP_PRIME_PRIME)
		msg_sop[port] = TCPCI_MSG_SOP_PRIME_PRIME;
#endif

	if (reg & ANX74XX_REG_EXT_HARD_RST) {
		/* hard reset received */
		task_set_event(PD_PORT_TO_TASK_ID(port),
			       PD_EVENT_RX_HARD_RESET);
	}
}

static int anx74xx_tcpm_init(int port)
{
	int rv = 0, reg;

	memset(&anx[port], 0, sizeof(struct anx_state));
	/* Bring chip in normal mode to work */
	anx74xx_set_power_mode(port, ANX74XX_NORMAL_MODE);

	/* Initialize analog section of ANX */
	rv |= anx74xx_init_analog(port);

	/* disable all interrupts */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_MASK_1,
			 ANX74XX_REG_CLEAR_SET_BITS);

	/* Initialize interrupt open-drain */
	rv |= tcpc_read(port, ANX74XX_REG_INTP_VCONN_CTRL, &reg);
	if (tcpc_config[port].flags & TCPC_FLAGS_ALERT_OD)
		reg |= ANX74XX_REG_R_INTERRUPT_OPEN_DRAIN;
	else
		reg &= ~ANX74XX_REG_R_INTERRUPT_OPEN_DRAIN;
	rv |= tcpc_write(port, ANX74XX_REG_INTP_VCONN_CTRL, reg);

	/* Initialize interrupt polarity */
	reg = tcpc_config[port].flags & TCPC_FLAGS_ALERT_ACTIVE_HIGH ?
		      ANX74XX_REG_IRQ_POL_HIGH :
		      ANX74XX_REG_IRQ_POL_LOW;
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_STATUS, reg);

	/* unmask interrupts */
	rv |= tcpc_read(port, ANX74XX_REG_IRQ_EXT_MASK_1, &reg);
	reg &= (~ANX74XX_REG_ALERT_TX_MSG_ERROR);
	reg &= (~ANX74XX_REG_ALERT_TX_CABLE_RESETOK);
	reg &= (~ANX74XX_REG_ALERT_TX_HARD_RESETOK);
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_MASK_1, reg);

	rv |= tcpc_read(port, ANX74XX_REG_IRQ_EXT_MASK_2, &reg);
	reg &= (~ANX74XX_REG_EXT_HARD_RST);
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_MASK_2, reg);

	/*  HPD pin output enable*/
	rv |= tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, ANX74XX_REG_HPD_DEFAULT);

	if (rv)
		return EC_ERROR_UNKNOWN;

	/* Set AVDD10_BMC to 1.08 */
	rv |= tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_5, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	rv = tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_5, (reg & 0xf3));
	if (rv)
		return EC_ERROR_UNKNOWN;

	/* Decrease BMC TX lowest swing voltage */
	rv |= tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_11, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	rv = tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_11, (reg & 0x3f) | 0x40);
	if (rv)
		return EC_ERROR_UNKNOWN;

	/* Set BMC TX cap slew rate to 400ns */
	rv = tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_12, 0x4);
	if (rv)
		return EC_ERROR_UNKNOWN;

	tcpm_get_chip_info(port, 1, NULL);

	return EC_SUCCESS;
}

static int anx74xx_get_chip_info(int port, int live,
				 struct ec_response_pd_chip_info_v1 *chip_info)
{
	int rv = tcpci_get_chip_info(port, live, chip_info);
	int val;

	if (rv)
		return rv;

	if (chip_info->fw_version_number == 0 ||
	    chip_info->fw_version_number == -1 || live) {
		rv = tcpc_read(port, ANX74XX_REG_FW_VERSION, &val);

		if (rv)
			return rv;

		chip_info->fw_version_number = val;
	}

#ifdef CONFIG_USB_PD_TCPM_ANX3429
	/*
	 * Min firmware version of ANX3429 to ensure that false SOP' detection
	 * doesn't occur for e-marked cables. See b/116255749#comment8 and
	 * b/64752060#comment11
	 */
	chip_info->min_req_fw_version_number = 0x16;
#endif

	return rv;
}

/*
 * Dissociate from the TCPC.
 */

static int anx74xx_tcpm_release(int port)
{
	return EC_SUCCESS;
}

const struct tcpm_drv anx74xx_tcpm_drv = {
	.init = &anx74xx_tcpm_init,
	.release = &anx74xx_tcpm_release,
	.get_cc = &anx74xx_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &anx74xx_tcpm_check_vbus_level,
#endif
	.select_rp_value = &anx74xx_tcpm_select_rp_value,
	.set_cc = &anx74xx_tcpm_set_cc,
	.set_polarity = &anx74xx_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &anx74xx_tcpm_set_vconn,
	.set_msg_header = &anx74xx_tcpm_set_msg_header,
	.set_rx_enable = &anx74xx_tcpm_set_rx_enable,
	.get_message_raw = &anx74xx_tcpm_get_message_raw,
	.transmit = &anx74xx_tcpm_transmit,
	.tcpc_alert = &anx74xx_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &anx74xx_tcpc_discharge_vbus,
#endif
	.get_chip_info = &anx74xx_get_chip_info,
#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) && \
	defined(CONFIG_USB_PD_TCPC_LOW_POWER)
	.drp_toggle = &anx74xx_tcpc_drp_toggle,
	.enter_low_power_mode = &anx74xx_enter_low_power_mode,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
struct i2c_stress_test_dev anx74xx_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = ANX74XX_REG_VENDOR_ID_L,
		.read_val = ANX74XX_VENDOR_ID & 0xFF,
		.write_reg = ANX74XX_REG_CC_SOFTWARE_CTRL,
	},
	.i2c_read = &tcpc_i2c_read,
	.i2c_write = &tcpc_i2c_write,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_TCPC */
