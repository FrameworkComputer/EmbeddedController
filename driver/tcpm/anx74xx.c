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
	int	pull;
	int	vconn_en;
	int	mux_state;
};
static struct anx_state anx[CONFIG_USB_PD_PORT_COUNT];

static int anx74xx_set_mux(int port, int polarity);

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

void anx74xx_tcpc_discharge_vbus(int port, int enable)
{
	int reg;

	tcpc_read(port, ANX74XX_REG_HPD_CTRL_0, &reg);
	if (enable)
		reg |= ANX74XX_REG_DISCHARGE_CTRL;
	else
		reg &= ~ANX74XX_REG_DISCHARGE_CTRL;
	tcpc_write(port, ANX74XX_REG_HPD_CTRL_0, reg);
}

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
	int  rv = EC_SUCCESS;

	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_2,
			 ANX74XX_REG_MODE_TRANS);
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_1,
			 0x0);
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_5,
			 0x04);
	rv |= tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_2,
			 0x0);
	if (rv)
		return EC_ERROR_UNKNOWN;
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
	} else
		return EC_ERROR_UNIMPLEMENTED;

	rv = tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_1, val);
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

static int anx74xx_send_message(int port, uint16_t header,
				const uint32_t *payload,
				int type,
				uint8_t len)
{
	int reg, rv = EC_SUCCESS;
	uint8_t *buf = NULL;
	int num_retry = 0, i = 0;


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

	/* Clear receive message interrupt bit(bit-0) */
	rv |= tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg);
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG,
			 reg & (~0x01));

	return rv;
}

static int anx74xx_tcpm_get_cc(int port, int *cc1, int *cc2)
{
	int rv = EC_SUCCESS;
	int reg = 0;
	int status = 0;

	rv |= tcpc_read(port, ANX74XX_REG_ANALOG_STATUS, &reg);

	if (!anx[port].pull) {/* get CC in sink mode */
		rv |= tcpc_read(port, ANX74XX_REG_POWER_DOWN_CTRL, &reg);
		if (rv)
			return EC_ERROR_UNKNOWN;
		/* CC1 */
		if (reg & ANX74XX_REG_STATUS_CC1_VRD_USB)
			*cc1 = TYPEC_CC_VOLT_SNK_DEF;
		else if (reg & ANX74XX_REG_STATUS_CC1_VRD_1P5)
			*cc1 = TYPEC_CC_VOLT_SNK_1_5;
		else if (reg & ANX74XX_REG_STATUS_CC1_VRD_3P0)
			*cc1 = TYPEC_CC_VOLT_SNK_3_0;
		else
			*cc1 = TYPEC_CC_VOLT_OPEN;
		/* CC2 */
		if (reg & ANX74XX_REG_STATUS_CC2_VRD_USB)
			*cc2 = TYPEC_CC_VOLT_SNK_DEF;
		else if (reg & ANX74XX_REG_STATUS_CC2_VRD_1P5)
			*cc2 = TYPEC_CC_VOLT_SNK_1_5;
		else if (reg & ANX74XX_REG_STATUS_CC2_VRD_3P0)
			*cc2 = TYPEC_CC_VOLT_SNK_3_0;
		else
			*cc2 = TYPEC_CC_VOLT_OPEN;
	} else {/* get CC in source mode */
		rv |= tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_7, &reg);
		if (rv)
			return EC_ERROR_UNKNOWN;
		/* CC1 */
		status = ANX74XX_REG_STATUS_CC1(reg);
		if (status) {
			if ((status & ANX74XX_REG_STATUS_CC_RA) ==
			    ANX74XX_REG_STATUS_CC_RA)
				*cc1 = TYPEC_CC_VOLT_RA;
			else if (status & ANX74XX_REG_STATUS_CC_RD)
				*cc1 = TYPEC_CC_VOLT_RD;
		} else {
				*cc1 = TYPEC_CC_VOLT_OPEN;
		}
		/* CC2 */
		status = ANX74XX_REG_STATUS_CC2(reg);
		if (status) {
			if ((status & ANX74XX_REG_STATUS_CC_RA) ==
			    ANX74XX_REG_STATUS_CC_RA)
				*cc2 = TYPEC_CC_VOLT_RA;
			else if (status & ANX74XX_REG_STATUS_CC_RD)
				*cc2 = TYPEC_CC_VOLT_RD;
		} else {
				*cc2 = TYPEC_CC_VOLT_OPEN;
		}
	}
	/* clear HPD status*/
	if (!(*cc1) && !(*cc2)) {
		anx74xx_tcpc_clear_hpd_status(port);
#ifdef CONFIG_USB_PD_TCPM_MUX
		anx74xx_tcpm_mux_exit(port);
#endif
	}

	return EC_SUCCESS;
}

static int anx74xx_tcpm_set_cc(int port, int pull)
{
	int rv = EC_SUCCESS;
	int reg;

	/* Enable CC software Control */
	rv |= tcpc_read(port, ANX74XX_REG_CC_SOFTWARE_CTRL, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	reg |= ANX74XX_REG_CC_SW_CTRL_ENABLE;
	rv |= tcpc_write(port, ANX74XX_REG_CC_SOFTWARE_CTRL, reg);
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
		anx[port].pull = 1;
		break;
	case TYPEC_CC_RD:
	/* Enable Rd */
		rv |= tcpc_read(port, ANX74XX_REG_ANALOG_STATUS, &reg);
		if (rv)
			return EC_ERROR_UNKNOWN;
		reg &= ANX74XX_REG_CC_PULL_RD;
		rv |= tcpc_write(port, ANX74XX_REG_ANALOG_STATUS, reg);
		anx[port].pull = 0;
		break;
	default:
		rv = EC_ERROR_UNKNOWN;
		break;
	}

	return rv;
}

static int anx74xx_tcpm_set_polarity(int port, int polarity)
{
	int reg, rv = EC_SUCCESS;

	rv |= tcpc_read(port, ANX74XX_REG_CC_SOFTWARE_CTRL, &reg);
	if (polarity) /* Inform ANX to use CC2 */
		reg &= ~ANX74XX_REG_SELECT_CC1;
	else /* Inform ANX to use CC1 */
		reg |= ANX74XX_REG_SELECT_CC1;
	rv |= tcpc_write(port, ANX74XX_REG_CC_SOFTWARE_CTRL, reg);

	anx[port].polarity = polarity;
	rv |= anx74xx_set_mux(port, polarity);

	/* Default DP pin mux D */
#ifdef CONFIG_USB_PD_TCPM_MUX
	anx74xx_tcpm_mux_set(port, TYPEC_MUX_DOCK);
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

	/* Read TCPC Alert register1 */
	rv |= tcpc_read(port, ANX74XX_REG_IRQ_EXT_SOURCE_1, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	/* Clears interrupt bits */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_1, reg);

	*alert = reg;
	rv = tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;

	if (reg & ANX74XX_REG_IRQ_CC_MSG_INT)
		*alert |= ANX74XX_REG_ALERT_MSG_RECV;
	else
		*alert &= (~ANX74XX_REG_ALERT_MSG_RECV);

	if (reg & ANX74XX_REG_IRQ_CC_STATUS_INT) {
		*alert |= ANX74XX_REG_ALERT_CC_CHANGE;
		rv |= tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG,
				reg & 0xfd);
	} else {
		*alert &= (~ANX74XX_REG_ALERT_CC_CHANGE);
	}

	if (reg & ANX74XX_REG_IRQ_GOOD_CRC_INT) {
		*alert |= ANX74XX_REG_ALERT_TX_ACK_RECV;
		rv |= tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG,
				reg & 0xfb);
	} else {
		*alert &= (~ANX74XX_REG_ALERT_TX_ACK_RECV);
	}

	if (reg & ANX74XX_REG_IRQ_TX_FAIL_INT) {
		*alert |= ANX74XX_REG_ALERT_TX_MSG_ERROR;
		rv |= tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG,
				reg & 0xf7);
	}
	/* Read TCPC Alert register2 */
	rv |= tcpc_read(port, ANX74XX_REG_IRQ_EXT_SOURCE_2, &reg);

	/* Clears interrupt bits */
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_EXT_SOURCE_2, reg);

	if (reg & ANX74XX_REG_EXT_HARD_RST)
		*alert |= ANX74XX_REG_ALERT_HARD_RST_RECV;
	else
		*alert &= (~ANX74XX_REG_ALERT_HARD_RST_RECV);

	return rv;
}

static int anx74xx_tcpm_set_rx_enable(int port, int enable)
{
	int reg, rv = 0;

	rv |= tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG_MASK, &reg);
	if (enable)
		reg &= ~(ANX74XX_REG_IRQ_CC_MSG_INT);
	else/* Disable RX message by masking interrupt */
		reg |= (ANX74XX_REG_IRQ_CC_MSG_INT);
	anx74xx_tcpm_set_auto_good_crc(port, enable);
	rv |= tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG_MASK, reg);

	return rv;
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
		*head = 0;
		/* Clear receive message interrupt bit(bit-0) */
		tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg);
		tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG,
			 reg & (~0x01));

		return EC_ERROR_UNKNOWN;
	}
	*head = reg;
	len = PD_HEADER_CNT(*head) * 4;
	if (!len) {
		/* Clear receive message interrupt bit(bit-0) */
		tcpc_read(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG, &reg);
		tcpc_write(port, ANX74XX_REG_IRQ_SOURCE_RECV_MSG,
			 reg & (~0x01));
		return EC_SUCCESS;
	}

	/* Receive message : assuming payload have enough
	 * memory allocated
	 */
	rv |= anx74xx_read_pd_obj(port, (uint8_t *)payload, len);
	if (rv) {
		*head = 0;
		return EC_ERROR_UNKNOWN;
	}

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

	/* Check the alert status */
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

	/* Set Pd dual role mode */
	pd_set_dual_role(PD_DRP_TOGGLE_ON);

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

	/* Decrease BMC TX lowest swing voltage */
	rv |= tcpc_read(port, ANX74XX_REG_ANALOG_CTRL_11, &reg);
	if (rv)
		return EC_ERROR_UNKNOWN;
	rv = tcpc_write(port, ANX74XX_REG_ANALOG_CTRL_11, (reg & 0x3f) | 0x40);
	if (rv)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

const struct tcpm_drv anx74xx_tcpm_drv = {
	.init			= &anx74xx_tcpm_init,
	.get_cc			= &anx74xx_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &anx74xx_tcpm_get_vbus_level,
#endif
	.set_cc			= &anx74xx_tcpm_set_cc,
	.set_polarity		= &anx74xx_tcpm_set_polarity,
	.set_vconn		= &anx74xx_tcpm_set_vconn,
	.set_msg_header		= &anx74xx_tcpm_set_msg_header,
	.set_rx_enable		= &anx74xx_tcpm_set_rx_enable,
	.get_message		= &anx74xx_tcpm_get_message,
	.transmit		= &anx74xx_tcpm_transmit,
	.tcpc_alert		= &anx74xx_tcpc_alert,
};
