/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager */

#include "task.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"

static int tcpc_vbus[CONFIG_USB_PD_PORT_COUNT];

static int init_alert_mask(int port)
{
	uint16_t mask;
	int rv;

	/*
	 * Create mask of alert events that will cause the TCPC to
	 * signal the TCPM via the Alert# gpio line.
	 */
	mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS
#ifdef CONFIG_USB_PD_TCPM_VBUS
		| TCPC_REG_ALERT_POWER_STATUS
#endif
		;
	/* Set the alert mask in TCPC */
	rv = tcpm_alert_mask_set(port, mask);

	return rv;
}

static int init_power_status_mask(int port)
{
	uint8_t mask;
	int rv;

#ifdef CONFIG_USB_PD_TCPM_VBUS
	mask = TCPC_REG_POWER_STATUS_VBUS_PRES;
#else
	mask = 0;
#endif
	rv = tcpm_set_power_status_mask(port, mask);

	return rv;
}

int tcpm_get_cc(int port, int *cc1, int *cc2)
{
	int status;
	int rv;

	rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);

	/* If tcpc read fails, return error */
	if (rv)
		return rv;

	*cc1 = TCPC_REG_CC_STATUS_CC1(status);
	*cc2 = TCPC_REG_CC_STATUS_CC2(status);

	/*
	 * If status is not open, then OR in termination to convert to
	 * enum tcpc_cc_voltage_status.
	 */
	if (*cc1 != TYPEC_CC_VOLT_OPEN)
		*cc1 |= TCPC_REG_CC_STATUS_TERM(status) << 2;
	if (*cc2 != TYPEC_CC_VOLT_OPEN)
		*cc2 |= TCPC_REG_CC_STATUS_TERM(status) << 2;

	return rv;
}

int tcpm_get_power_status(int port, int *status)
{
	return tcpc_read(port, TCPC_REG_POWER_STATUS, status);
}

int tcpm_set_cc(int port, int pull)
{
	/*
	 * Set manual control of Rp/Rd, and set both CC lines to the same
	 * pull.
	 */
	/* TODO: set desired Rp strength */
	return tcpc_write(port, TCPC_REG_ROLE_CTRL,
			  TCPC_REG_ROLE_CTRL_SET(0, 0, pull, pull));
}

int tcpm_set_polarity(int port, int polarity)
{
	return tcpc_write(port, TCPC_REG_TCPC_CTRL,
			  TCPC_REG_TCPC_CTRL_SET(polarity));
}

int tcpm_set_vconn(int port, int enable)
{
	return tcpc_write(port, TCPC_REG_POWER_CTRL,
			  TCPC_REG_POWER_CTRL_SET(enable));
}

int tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_write(port, TCPC_REG_MSG_HDR_INFO,
			  TCPC_REG_MSG_HDR_INFO_SET(data_role, power_role));
}

int tcpm_alert_status(int port, int *alert)
{
	/* Read TCPC Alert register */
	return tcpc_read16(port, TCPC_REG_ALERT, alert);
}

int tcpm_set_rx_enable(int port, int enable)
{
	/* If enable, then set RX detect for SOP and HRST */
	return tcpc_write(port, TCPC_REG_RX_DETECT,
			  enable ? TCPC_REG_RX_DETECT_SOP_HRST_MASK : 0);
}

int tcpm_set_power_status_mask(int port, uint8_t mask)
{
	/* write to the Alert Mask register */
	return tcpc_write(port, TCPC_REG_POWER_STATUS_MASK , mask);
}

int tcpm_alert_mask_set(int port, uint16_t mask)
{
	/* write to the Alert Mask register */
	return tcpc_write16(port, TCPC_REG_ALERT_MASK, mask);
}

#ifdef CONFIG_USB_PD_TCPM_VBUS
int tcpm_get_vbus_level(int port)
{
	return tcpc_vbus[port];
}
#endif

int tcpm_get_message(int port, uint32_t *payload, int *head)
{
	int rv, cnt, reg = TCPC_REG_RX_DATA;

	rv = tcpc_read(port, TCPC_REG_RX_BYTE_CNT, &cnt);

	rv |= tcpc_read16(port, TCPC_REG_RX_HDR, (int *)head);

	if (rv == EC_SUCCESS && cnt > 0) {
		tcpc_lock(port, 1);
		rv = tcpc_xfer(port,
			       (uint8_t *)&reg, 1, (uint8_t *)payload,
			       cnt, I2C_XFER_SINGLE);
		tcpc_lock(port, 0);
	}

	/* Read complete, clear RX status alert bit */
	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_STATUS);

	return rv;
}

int tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		   const uint32_t *data)
{
	int reg = TCPC_REG_TX_DATA;
	int rv, cnt = 4*PD_HEADER_CNT(header);

	/* TX_BYTE_CNT includes 2 bytes for message header */
	rv = tcpc_write(port, TCPC_REG_TX_BYTE_CNT, cnt + 2);

	rv |= tcpc_write16(port, TCPC_REG_TX_HDR, header);

	/* If tcpc read fails, return error */
	if (rv)
		return rv;

	if (cnt > 0) {
		tcpc_lock(port, 1);
		rv = tcpc_xfer(port,
			       (uint8_t *)&reg, 1, NULL, 0, I2C_XFER_START);
		rv |= tcpc_xfer(port,
				(uint8_t *)data, cnt, NULL, 0, I2C_XFER_STOP);
		tcpc_lock(port, 0);
	}

	/* If tcpc read fails, return error */
	if (rv)
		return rv;

	rv = tcpc_write(port, TCPC_REG_TRANSMIT, TCPC_REG_TRANSMIT_SET(type));

	return rv;
}

void tcpc_alert(int port)
{
	int status;

	/* Read the Alert register from the TCPC */
	tcpm_alert_status(port, &status);

	/*
	 * Clear alert status for everything except RX_STATUS, which shouldn't
	 * be cleared until we have successfully retrieved message.
	 */
	if (status & ~TCPC_REG_ALERT_RX_STATUS)
		tcpc_write16(port, TCPC_REG_ALERT,
			     status & ~TCPC_REG_ALERT_RX_STATUS);

	if (status & TCPC_REG_ALERT_CC_STATUS) {
		/* CC status changed, wake task */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
	}
	if (status & TCPC_REG_ALERT_POWER_STATUS) {
		int reg = 0;

		tcpc_read(port, TCPC_REG_POWER_STATUS_MASK, &reg);

		if (reg == TCPC_REG_POWER_STATUS_MASK_ALL) {
			/*
			 * If power status mask has been reset, then the TCPC
			 * has reset.
			 */
			task_set_event(PD_PORT_TO_TASK_ID(port),
				       PD_EVENT_TCPC_RESET, 0);
		} else {
			/* Read Power Status register */
			tcpm_get_power_status(port, &reg);
			/* Update VBUS status */
			tcpc_vbus[port] = reg &
				TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
#if defined(CONFIG_USB_PD_TCPM_VBUS) && defined(CONFIG_USB_CHARGER)
			/* Update charge manager with new VBUS state */
			usb_charger_vbus_change(port, tcpc_vbus[port]);
			task_wake(PD_PORT_TO_TASK_ID(port));
#endif /* CONFIG_USB_PD_TCPM_VBUS && CONFIG_USB_CHARGER */
		}
	}
	if (status & TCPC_REG_ALERT_RX_STATUS) {
		/* message received */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_RX, 0);
	}
	if (status & TCPC_REG_ALERT_RX_HARD_RST) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
	if (status & TCPC_REG_ALERT_TX_COMPLETE) {
		/* transmit complete */
		pd_transmit_complete(port, status & TCPC_REG_ALERT_TX_SUCCESS ?
					   TCPC_TX_COMPLETE_SUCCESS :
					   TCPC_TX_COMPLETE_FAILED);
	}
}

int tcpm_init(int port)
{
	int rv;
	int power_status;

	while (1) {
		rv = tcpc_read(port, TCPC_REG_POWER_STATUS, &power_status);
		/*
		 * If read succeeds and the uninitialized bit is clear, then
		 * initalization is complete, clear all alert bits and write
		 * the initial alert mask.
		 */
		if (rv == EC_SUCCESS &&
		    !(power_status & TCPC_REG_POWER_STATUS_UNINIT)) {
			tcpc_write16(port, TCPC_REG_ALERT, 0xffff);
			/* Initialize power_status_mask */
			init_power_status_mask(port);
			/* Update VBUS status */
			tcpc_vbus[port] = power_status &
				TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
			return init_alert_mask(port);
		}
		msleep(10);
	}
}

#ifdef CONFIG_USB_PD_TCPM_MUX

static int tcpm_mux_init(int i2c_addr)
{
	return EC_SUCCESS;
}

static int tcpm_mux_set(int i2c_addr, mux_state_t mux_state)
{
	int reg = 0;
	int rv;
	int port = i2c_addr; /* use port index in port_addr field */

	rv = tcpc_read(port, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	reg &= ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK;
	if (mux_state & MUX_USB_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	if (mux_state & MUX_DP_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP;
	/* MUX_POLARITY_INVERTED: polarity is handled by the TCPC */

	return tcpc_write(port, TCPC_REG_CONFIG_STD_OUTPUT, reg);
}

/* Reads control register and updates mux_state accordingly */
static int tcpm_mux_get(int i2c_addr, mux_state_t *mux_state)
{
	int reg = 0;
	int rv;
	int port = i2c_addr; /* use port index in port_addr field */

	*mux_state = 0;
	rv = tcpc_read(port, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP)
		*mux_state |= MUX_DP_ENABLED;
	/* MUX_POLARITY_INVERTED is always omitted */

	return EC_SUCCESS;
}


const struct usb_mux_driver tcpm_usb_mux_driver = {
	.init = tcpm_mux_init,
	.set = tcpm_mux_set,
	.get = tcpm_mux_get,
};

#endif /* CONFIG_USB_PD_TCPM_MUX */
