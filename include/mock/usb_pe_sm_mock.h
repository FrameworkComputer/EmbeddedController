/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock USB PE state machine */

#ifndef __MOCK_USB_PE_SM_MOCK_H
#define __MOCK_USB_PE_SM_MOCK_H

#include "common.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mock_pe_port_t {
	enum tcpci_msg_type sop;

	int mock_pe_message_sent;
	int mock_pe_error;
	int mock_pe_hard_reset_sent;
	int mock_pe_got_hard_reset;
	int mock_pe_message_received;
	int mock_got_soft_reset;
	int mock_pe_message_discarded;
};

extern struct mock_pe_port_t mock_pe_port[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_pe_port_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_USB_PE_SM_MOCK_H */
