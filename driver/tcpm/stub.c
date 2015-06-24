/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TCPM for MCU also running TCPC */

#include "task.h"
#include "tcpci.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

extern int tcpc_alert_status(int port, int *alert);
extern int tcpc_alert_status_clear(int port, uint16_t mask);
extern int tcpc_alert_mask_set(int port, uint16_t mask);
extern int tcpc_get_cc(int port, int *cc1, int *cc2);
extern int tcpc_set_cc(int port, int pull);
extern int tcpc_set_polarity(int port, int polarity);
extern int tcpc_set_vconn(int port, int enable);
extern int tcpc_set_msg_header(int port, int power_role, int data_role);
extern int tcpc_set_rx_enable(int port, int enable);

extern int tcpc_get_message(int port, uint32_t *payload, int *head);
extern int tcpc_transmit(int port, enum tcpm_transmit_type type,
			 uint16_t header, const uint32_t *data);

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
		TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS;
	/* Set the alert mask in TCPC */
	rv = tcpm_alert_mask_set(port, mask);

	return rv;
}

int tcpm_init(int port)
{
	tcpc_init(port);
	return init_alert_mask(port);
}

int tcpm_get_cc(int port, int *cc1, int *cc2)
{
	return tcpc_get_cc(port, cc1, cc2);
}

int tcpm_set_cc(int port, int pull)
{
	return tcpc_set_cc(port, pull);
}

int tcpm_set_polarity(int port, int polarity)
{
	return tcpc_set_polarity(port, polarity);
}

int tcpm_set_vconn(int port, int enable)
{
	return tcpc_set_vconn(port, enable);
}

int tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_set_msg_header(port, power_role, data_role);
}

int tcpm_alert_status(int port, int *alert)
{
	int rv;

	/* Read TCPC Alert register */
	rv = tcpc_alert_status(port, alert);
	/* Clear all bits being processed by the protocol layer */
	tcpc_alert_status_clear(port, *alert);
	return rv;
}

int tcpm_set_rx_enable(int port, int enable)
{
	return tcpc_set_rx_enable(port, enable);
}

int tcpm_alert_mask_set(int port, uint16_t mask)
{
	return tcpc_alert_mask_set(port, mask);
}

int tcpm_get_message(int port, uint32_t *payload, int *head)
{
	return tcpc_get_message(port, payload, head);
}

int tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		  const uint32_t *data)
{
	return tcpc_transmit(port, type, header, data);
}

void tcpc_alert(int port)
{
	int status;

	/* Read the Alert register from the TCPC */
	tcpm_alert_status(port, &status);

	if (status & TCPC_REG_ALERT_CC_STATUS) {
		/* CC status changed, wake task */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
	}
	if (status & TCPC_REG_ALERT_RX_STATUS) {
		/*
		 * message received. since TCPC is compiled in, we
		 * already received PD_EVENT_RX from phy layer in
		 * pd_rx_event(), so we don't need to set another
		 * event.
		 */
	}
	if (status & TCPC_REG_ALERT_RX_HARD_RST) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
	if (status & TCPC_REG_ALERT_TX_COMPLETE) {
		/* transmit complete */
		pd_transmit_complete(port, status & TCPC_REG_ALERT_TX_COMPLETE);
	}
}
