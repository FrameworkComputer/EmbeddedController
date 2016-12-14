/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Author : Analogix Semiconductor.
 */

/* Type-C port manager for Analogix's anx74xx chips */

#include "anx74xx.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"

struct anx_state {
	int	polarity;
	int	vconn_en;
	int	mux_state;
};
#define clear_recvd_msg_int(port) do {\
		int reg, rv; \
		rv = tcpc_read(port, ANX74XX_REG_RECVD_MSG_INT, &reg); \
		if (!rv) \
			tcpc_write(port, ANX74XX_REG_RECVD_MSG_INT, \
			reg | 0x01); \
	} while (0)

static struct anx_state anx[CONFIG_USB_PD_PORT_COUNT];

static int anx74xx_set_mux(int port, int polarity);

/* Save the selected rp value */
static int selected_rp[CONFIG_USB_PD_PORT_COUNT];

static void anx74xx_tcpm_set_auto_good_crc(int port, int enable)
{
	int reg;

	if (enable) {
		/* Set default header for Good CRC auto reply */
		tcpc_read(port, ANX74XX_REG_TX_MSG_HEADER, &reg);
		reg |= (PD_REV20 << ANX74XX_REG_SPEC_REV_BIT_POS);
		reg |= ANX74XX_REG_AUTO_GOODCRC_EN;
		tcpc_write(port, ANX74XX_REG_TX_MSG_HEADER, reg);

		reg = ANX74XX_REG_ENABLE_GOODCRC;
		tcpc_write(port, ANX74XX_REG_TX_AUTO_GOODCRC_2, reg);
		/* Set bit-0 if enable, reset bit-0 if disable */
		tcpc_read(port, ANX74XX_REG_TX_AUTO_GOODCRC_1, &reg);
		reg |= ANX74XX_REG_AUTO_GOODCRC_EN;
	} else {
		/* Clear bit-0 for disable */
		tcpc_read(port, ANX74XX_REG_TX_AUTO_GOODCRC_1, &reg);
		reg &= ~ANX74XX_REG_AUTO_GOODCRC_EN;
		tcpc_write(port, ANX74XX_REG_TX_AUTO_GOODCRC_2, 0);
	}
	tcpc_write(port, ANX74XX_REG_TX_AUTO_GOODCRC_1, reg);
}

static void anx74xx_set_power_mode(int port, int mode)
{
	switch (mode) {
	case ANX74XX_NORMAL_MODE:
	/* Set PWR_EN and RST_N GPIO pins high */
		board_set_tcpc_power_mode(port, 1);
		break;
	case ANX74XX_STANDBY_MODE:
	/* Disable PWR_EN, keep Digital and analog block
	 *  ON for cable detection
	 */
		board_set_tcpc_power_mode(port, 0);
		break;
	default:
		break;
	}
}

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

void anx74xx_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq)
{
	int reg;

	tcpc_read(port, ANX74XX_REG_HPD_CTRL_0, &reg);
	if (hpd_lvl)
		reg |= ANX74XX_REG_HPD_OUT_DATA;
	else
		reg &= ~ANX74XX_REG_HPD_OUT_DATA;
	tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);

	if (hpd_irq) {
		tcpc_read(port, ANX74XX_REG_HPD_CTRL_0, &reg);
		reg &= ~ANX74XX_REG_HPD_OUT_DATA;
		tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);
		msleep(1);
		reg |= ANX74XX_REG_HPD_OUT_DATA;
		tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);
	}
}

void anx74xx_tcpc_clear_hpd_status(int port)
{
	int reg;

	tcpc_read(port, ANX74XX_REG_HPD_CTRL_0, &reg);
	reg &= 0xcf;
	tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);
}

#ifdef CONFIG_USB_PD_TCPM_MUX
static int anx74xx_tcpm_mux_init(int i2c_addr)
{
	int port = i2c_addr;

	/* Nothing to do here, ANX initializes its muxes
	 * as (MUX_USB_ENABLED | MUX_DP_ENABLED)
	 */
	anx[port].mux_state = MUX_USB_ENABLED | MUX_DP_ENABLED;

	return EC_SUCCESS;
}

static int anx74xx_tcpm_mux_exit(int port)
{
	int rv = EC_SUCCESS;
	int reg = 0x0;

	rv = tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_2, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_2, reg | ANX74XX_REG_MODE_TRANS);

	/* Clear Bit[7:0] R_SWITCH */
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_1, 0x0);

	/* Clear Bit[7:4] R_SWITCH_H */
	rv |= tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_5, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_5, (reg & 0x0f));

	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_2, reg & 0x09);
	if (rv)
		return EC_ERROR_UNKNOWN;

	return rv;

}


static int anx74xx_set_mux(int port, int polarity)
{
	int reg, rv = EC_SUCCESS;

	rv = tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_2, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	if (polarity) {
		reg |= ANX74XX_REG_AUX_SWAP_SET_CC2;
		reg &= ~ANX74XX_REG_AUX_SWAP_SET_CC1;
	} else {
		reg |= ANX74XX_REG_AUX_SWAP_SET_CC1;
		reg &= ~ANX74XX_REG_AUX_SWAP_SET_CC2;
	}
	rv = tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_2, reg);

	return rv;
}

static int anx74xx_tcpm_mux_set(int i2c_addr, mux_state_t mux_state)
{
	int reg = 0, val = 0;
	int rv;
	int port = i2c_addr;

	if (!(mux_state & ~MUX_POLARITY_INVERTED)) {
		anx[port].mux_state = mux_state;
		return anx74xx_tcpm_mux_exit(port);
	}

	rv = tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_5, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	reg &= 0x0f;

	if (mux_state & MUX_USB_ENABLED) {
		/* Set pin assignment D */
		if (mux_state & MUX_POLARITY_INVERTED) {
			val = ANX74XX_REG_MUX_DP_MODE_BDF_CC2;
			reg |= ANX74XX_REG_MUX_SSTX_B;
		} else {
			val = ANX74XX_REG_MUX_DP_MODE_BDF_CC1;
			reg |= ANX74XX_REG_MUX_SSTX_A;
		}
	} else if (mux_state & MUX_DP_ENABLED) {
		/* Set pin assignment C */
		if (mux_state & MUX_POLARITY_INVERTED) {
			val = ANX74XX_REG_MUX_DP_MODE_ACE_CC2;
			reg |= ANX74XX_REG_MUX_ML2_B;
		} else {
			val = ANX74XX_REG_MUX_DP_MODE_ACE_CC1;
			reg |= ANX74XX_REG_MUX_ML2_A;
		}
		/* FIXME: disabling DP mode should disable SBU muxes */
		rv |= anx74xx_set_mux(port, mux_state & MUX_POLARITY_INVERTED);
	} else if (!mux_state) {
		return anx74xx_tcpm_mux_exit(port);
	} else {
		return  EC_ERROR_UNIMPLEMENTED;
	}

	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_1, val);
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_5, reg);

	anx74xx_set_mux(port, mux_state & MUX_POLARITY_INVERTED ? 1 : 0);

	anx[port].mux_state = mux_state;

	return rv;
}

/* current mux state  */
static int anx74xx_tcpm_mux_get(int i2c_addr, mux_state_t *mux_state)
{
	int port = i2c_addr;

	*mux_state = anx[port].mux_state;

	return EC_SUCCESS;
}

const struct usb_mux_driver anx74xx_tcpm_usb_mux_driver = {
	.init = anx74xx_tcpm_mux_init,
	.set = anx74xx_tcpm_mux_set,
	.get = anx74xx_tcpm_mux_get,
};
#endif /* CONFIG_USB_PD_TCPM_MUX */

static int anx74xx_init_analog(int port)
{
	int reg, rv = EC_SUCCESS;

	/* Analog settings for chip */
	rv |= tcpc_write(port, ANX74XX_REG_HPD_CONTROL,
			 ANX74XX_REG_HPD_OP_MODE);
	rv |= tcpc_write(port, ANX74XX_REG_HPD_CTRL_0,
			 ANX74XX_REG_HPD_DEFAULT);
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
				const uint32_t *payload,
				int type,
				uint8_t len)
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
		rv |= tcpc_write(
			port, ANX74XX_REG_CTRL_FW, reg | CLEAR_RX_BUFFER);
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
					ANX74XX_REG_TX_START_ADDR_1 + i - 18,
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

static int anx74xx_read_pd_obj(int port,
				uint8_t *buf,
				int plen)
{
	int rv = EC_SUCCESS, i;
	int reg, addr = ANX74XX_REG_PD_RX_DATA_OBJ;

	/* Read PD data objects from ANX */
	for (i = 0; i < plen ; i++) {
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
		cc = TYPEC_CC_VOLT_SNK_DEF;
		break;

	case BIT_VALUE_OF_SNK_CC_1_P_5:
		cc = TYPEC_CC_VOLT_SNK_1_5;
		break;

	case BIT_VALUE_OF_SNK_CC_3_P_0:
		cc = TYPEC_CC_VOLT_SNK_3_0;
		break;

	default:
		/* If no bits are set, then nothing is attached */
		cc = TYPEC_CC_VOLT_OPEN;
	}

	return cc;
}

static int anx74xx_tcpm_get_cc(int port, int *cc1, int *cc2)
{
	int rv = EC_SUCCESS;
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
	/* For ANX3429 cannot get cc correctly when Rp != USB_Default */
	selected_rp[port] = rp;
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

static int anx74xx_tcpm_set_polarity(int port, int polarity)
{
	int reg, mux_state, rv = EC_SUCCESS;

	rv |= tcpc_read(port, ANX74XX_REG_CC_SOFTWARE_CTRL, &reg);
	if (polarity) /* Inform ANX to use CC2 */
		reg &= ~ANX74XX_REG_SELECT_CC1;
	else /* Inform ANX to use CC1 */
		reg |= ANX74XX_REG_SELECT_CC1;
	rv |= tcpc_write(port, ANX74XX_REG_CC_SOFTWARE_CTRL, reg);

	anx[port].polarity = polarity;

	/* Update mux polarity */
#ifdef CONFIG_USB_PD_TCPM_MUX
	mux_state = anx[port].mux_state & ~MUX_POLARITY_INVERTED;
	if (polarity)
		mux_state |= MUX_POLARITY_INVERTED;
	anx74xx_tcpm_mux_set(port, mux_state);
#endif
	return rv;
}

#ifdef CONFIG_USB_PD_TCPC_FW_VERSION
int anx74xx_tcpc_get_fw_version(int port, int *version)
{
	return tcpc_read(port, ANX74XX_REG_FW_VERSION, version);
}
#endif

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

	return rv;
}

static int anx74xx_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	int rv = 0, reg;

	rv |= tcpc_read(port, ANX74XX_REG_TX_MSG_HEADER, &reg);
	reg |= ((!!power_role) << ANX74XX_REG_PWR_ROLE_BIT_POS);
	reg |= ((!!data_role) << ANX74XX_REG_DATA_ROLE_BIT_POS);
	rv |= tcpc_write(port, ANX74XX_REG_TX_MSG_HEADER, reg);

	return rv;
}

static int anx74xx_alert_status(int port, int *alert)
{
	int reg, rv = EC_SUCCESS;

	/* Clear soft irq bit */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_3,
			 ANX74XX_REG_CLEAR_SOFT_IRQ);
	*alert = 0;
	rv = tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	/*Clear msg received bit, until read it by TCPM*/
	rv |= tcpc_write(port, ANX74XX_REG_RECVD_MSG_INT, (reg & 0xFE));

	if (reg & ANX74XX_REG_IRQ_CC_MSG_INT)
		*alert |= ANX74XX_REG_ALERT_MSG_RECV;

	if (reg & ANX74XX_REG_IRQ_CC_STATUS_INT)
		*alert |= ANX74XX_REG_ALERT_CC_CHANGE;

	if (reg & ANX74XX_REG_IRQ_GOOD_CRC_INT)
		*alert |= ANX74XX_REG_ALERT_TX_ACK_RECV;

	if (reg & ANX74XX_REG_IRQ_TX_FAIL_INT)
		*alert |= ANX74XX_REG_ALERT_TX_MSG_ERROR;

	rv |= tcpc_read(port, ANX74XX_REG_IRQ_EXT_SOURCE_1, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	/* Clears interrupt bits */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_1, reg);

	/* Check for Hard Reset done bit */
	if (reg & ANX74XX_REG_ALERT_TX_HARD_RESETOK)
		*alert |= ANX74XX_REG_ALERT_TX_HARD_RESETOK;

	/* Read TCPC Alert register2 */
	rv |= tcpc_read(port, ANX74XX_REG_IRQ_EXT_SOURCE_2, &reg);

	/* Clears interrupt bits */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_2, reg);

	if (reg & ANX74XX_REG_EXT_HARD_RST)
		*alert |= ANX74XX_REG_ALERT_HARD_RST_RECV;

	return rv;
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
		anx74xx_rp_control(port, selected_rp[port]);
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
static int anx74xx_tcpm_get_vbus_level(int port)
{
	int reg = 0;

	tcpc_read(port, ANX74XX_REG_ANALOG_STATUS, &reg);
	return ((reg & ANX74XX_REG_VBUS_STATUS) ? 1 : 0);
}
#endif

static int anx74xx_tcpm_get_message(int port, uint32_t *payload, int *head)
{
	int reg = 0, rv = EC_SUCCESS;
	int len = 0;

	/* Fetch the header */
	rv |= tcpc_read16(port, ANX74XX_REG_PD_HEADER, &reg);
	if (rv) {
		clear_recvd_msg_int(port);
		return EC_ERROR_UNKNOWN;
	}
	*head = reg;

	len = PD_HEADER_CNT(*head) * 4;
	if (!len) {
		clear_recvd_msg_int(port);
		return EC_SUCCESS;
	}

	/* Receive message : assuming payload have enough
	 * memory allocated
	 */
	rv |= anx74xx_read_pd_obj(port, (uint8_t *)payload, len);
	if (rv)
		return EC_ERROR_UNKNOWN;

	return rv;
}

static int anx74xx_tcpm_transmit(int port, enum tcpm_transmit_type type,
		  uint16_t header,
		  const uint32_t *data)
{
	uint8_t len = 0;
	int ret = 0, reg = 0;

	switch (type) {
	/* ANX is aware of type */
	case TCPC_TX_SOP:
	case TCPC_TX_SOP_PRIME:
	case TCPC_TX_SOP_PRIME_PRIME:
		len = PD_HEADER_CNT(header) * 4 + 2;
		ret = anx74xx_send_message(port, header,
						  data, type, len);
		break;
	case TCPC_TX_HARD_RESET:
	/* Request HARD RESET */
		tcpc_read(port, ANX74XX_REG_TX_CTRL_1, &reg);
		reg |= ANX74XX_REG_TX_HARD_RESET_REQ;
		ret = tcpc_write(port, ANX74XX_REG_TX_CTRL_1, reg);
	/*After Hard Reset, TCPM shall disable goodCRC*/
		anx74xx_tcpm_set_auto_good_crc(port, 0);
		break;
	case TCPC_TX_CABLE_RESET:
	/* Request CABLE RESET */
		tcpc_read(port, ANX74XX_REG_TX_CTRL_1, &reg);
		reg |= ANX74XX_REG_TX_CABLE_RESET_REQ;
		ret = tcpc_write(port, ANX74XX_REG_TX_CTRL_1, reg);
		break;
	case TCPC_TX_BIST_MODE_2:
	/* Request BIST MODE 2 */
		reg = ANX74XX_REG_TX_BIST_START
				| ANX74XX_REG_TX_BIXT_FOREVER | (0x02 << 4);
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

void anx74xx_tcpc_alert(int port)
{
	int status;

	/* Check the alert status from anx74xx */
	if (anx74xx_alert_status(port, &status))
		status = 0;
	if (status) {

		if (status & ANX74XX_REG_ALERT_CC_CHANGE) {
			/* CC status changed, wake task */
			task_set_event(PD_PORT_TO_TASK_ID(port),
					PD_EVENT_CC, 0);
		}

		/* If alert is to receive a message */
		if (status & ANX74XX_REG_ALERT_MSG_RECV) {
			/* Set a PD_EVENT_RX */
			task_set_event(PD_PORT_TO_TASK_ID(port),
						PD_EVENT_RX, 0);
		}
		if (status & ANX74XX_REG_ALERT_TX_ACK_RECV) {
			/* Inform PD about this TX success */
			pd_transmit_complete(port,
						TCPC_TX_COMPLETE_SUCCESS);
		}
		if (status & ANX74XX_REG_ALERT_TX_MSG_ERROR) {
			/* let PD does not wait for this */
			pd_transmit_complete(port,
					      TCPC_TX_COMPLETE_FAILED);
		}
		if (status & ANX74XX_REG_ALERT_TX_CABLE_RESETOK) {
			/* ANX hardware clears the request bit */
			pd_transmit_complete(port,
					      TCPC_TX_COMPLETE_SUCCESS);
		}
		if (status & ANX74XX_REG_ALERT_TX_HARD_RESETOK) {
			/* ANX hardware clears the request bit */
			pd_transmit_complete(port,
					     TCPC_TX_COMPLETE_SUCCESS);
		}
		if (status & ANX74XX_REG_ALERT_HARD_RST_RECV) {
			/* hard reset received */
			pd_execute_hard_reset(port);
			task_wake(PD_PORT_TO_TASK_ID(port));
		}
	}
}

int anx74xx_tcpm_init(int port)
{
	int rv = 0, reg;

	memset(anx, 0, CONFIG_USB_PD_PORT_COUNT*sizeof(struct anx_state));
	/* Bring chip in normal mode to work */
	anx74xx_set_power_mode(port, ANX74XX_NORMAL_MODE);

	/* Initialize analog section of ANX */
	rv |= anx74xx_init_analog(port);

	/* disable all interrupts */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_MASK_1,
			 ANX74XX_REG_CLEAR_SET_BITS);

	/* Initialize interrupt polarity */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_STATUS,
			tcpc_config[port].pol == TCPC_ALERT_ACTIVE_LOW ?
			ANX74XX_REG_IRQ_POL_LOW :
			ANX74XX_REG_IRQ_POL_HIGH);

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

#ifdef CONFIG_USB_PD_TCPC_FW_VERSION
	board_print_tcpc_fw_version(port);
#endif

	return EC_SUCCESS;
}

const struct tcpm_drv anx74xx_tcpm_drv = {
	.init			= &anx74xx_tcpm_init,
	.get_cc			= &anx74xx_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &anx74xx_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &anx74xx_tcpm_select_rp_value,
	.set_cc			= &anx74xx_tcpm_set_cc,
	.set_polarity		= &anx74xx_tcpm_set_polarity,
	.set_vconn		= &anx74xx_tcpm_set_vconn,
	.set_msg_header		= &anx74xx_tcpm_set_msg_header,
	.set_rx_enable		= &anx74xx_tcpm_set_rx_enable,
	.get_message		= &anx74xx_tcpm_get_message,
	.transmit		= &anx74xx_tcpm_transmit,
	.tcpc_alert		= &anx74xx_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &anx74xx_tcpc_discharge_vbus,
#endif
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
