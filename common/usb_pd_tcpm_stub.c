/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TCPM for MCU also running TCPC */

#include "usb_pd.h"
#include "usb_pd_tcpm.h"

extern int tcpc_alert_status(int port, int alert_reg, uint8_t *alert);
extern int tcpc_get_cc(int port, int *cc1, int *cc2);
extern int tcpc_set_cc(int port, int pull);
extern int tcpc_set_polarity(int port, int polarity);
extern int tcpc_set_vconn(int port, int enable);
extern int tcpc_set_msg_header(int port, int power_role, int data_role);
extern int tcpc_set_rx_enable(int port, int enable);

extern int tcpc_get_message(int port, uint32_t *payload, int *head);
extern int tcpc_transmit(int port, enum tcpm_transmit_type type,
			 uint16_t header, const uint32_t *data);

int tcpm_init(int port)
{
	tcpc_init(port);
	return EC_SUCCESS;
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

int tcpm_alert_status(int port, int alert_reg, uint8_t *alert)
{
	return tcpc_alert_status(port, alert_reg, alert);
}

int tcpm_set_rx_enable(int port, int enable)
{
	return tcpc_set_rx_enable(port, enable);
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
