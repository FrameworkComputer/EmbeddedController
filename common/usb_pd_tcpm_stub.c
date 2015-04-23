/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"
#include "usb_pd_tcpm.h"

extern int tcpc_alert_status(int port, int alert_reg);
extern int tcpc_get_cc(int port, int polarity);
extern void tcpc_set_cc(int port, int pull);
extern void tcpc_set_polarity(int port, int polarity);
extern void tcpc_set_vconn(int port, int enable);
extern void tcpc_set_msg_header(int port, int power_role, int data_role);

extern int tcpc_get_message(int port, uint32_t *payload);
extern void tcpc_transmit(int port, enum tcpm_transmit_type type,
			  uint16_t header, const uint32_t *data);

int tcpm_get_cc(int port, int polarity)
{
	return tcpc_get_cc(port, polarity);
}

void tcpm_set_cc(int port, int pull)
{
	return tcpc_set_cc(port, pull);
}

void tcpm_set_polarity(int port, int polarity)
{
	return tcpc_set_polarity(port, polarity);
}

void tcpm_set_vconn(int port, int enable)
{
	return tcpc_set_vconn(port, enable);
}

void tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_set_msg_header(port, power_role, data_role);
}

int tcpm_alert_status(int port, int alert_reg)
{
	return tcpc_alert_status(port, alert_reg);
}

int tcpm_get_message(int port, uint32_t *payload)
{
	return tcpc_get_message(port, payload);
}

void tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		   const uint32_t *data)
{
	return tcpc_transmit(port, type, header, data);
}
