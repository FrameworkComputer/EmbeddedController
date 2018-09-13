/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"

/* USB Policy Engine Charge-Through VCONN Powered Device module */

#ifndef __CROS_EC_USB_PE_CTVPD_H
#define __CROS_EC_USB_PE_CTVPD_H

/* Policy Engine Flags */
#define PE_FLAGS_MSG_RECEIVED (1 << 0)

enum l_state {
	PE_INIT,
	PE_RUN,
	PE_PAUSED
};

static enum l_state local_state = PE_INIT;

/*
 * PE_OBJ is a convenience macro to access struct sm_obj, which
 * must be the first member of struct policy_engine.
 */
#define PE_OBJ(port)   (SM_OBJ(pe[port]))

/**
 * This is the PE Port object that contains information needed to
 * implement a VCONN and Charge-Through VCONN Powered Device.
 */
static struct policy_engine {
	/*
	 * struct sm_obj must be first. This is the state machine
	 * object that keeps track of the current and last state
	 * of the state machine.
	 */
	struct sm_obj obj;
	/* port flags, see PE_FLAGS_* */
	uint32_t flags;
} pe[CONFIG_USB_PD_PORT_COUNT];

static unsigned int pe_request(int port, enum signal sig);
static unsigned int pe_request_entry(int port);
static unsigned int pe_request_run(int port);

static unsigned int do_nothing_exit(int port);
static unsigned int get_super_state(int port);

static const state_sig pe_request_sig[] = {
	pe_request_entry,
	pe_request_run,
	do_nothing_exit,
	get_super_state
};

void pe_init(int port)
{
	pe[port].flags = 0;
	init_state(port, PE_OBJ(port), pe_request);
}

void policy_engine(int port, int evt, int en)
{
	switch (local_state) {
	case PE_INIT:
		pe_init(port);
		local_state = PE_RUN;
		/* fall through */
	case PE_RUN:
		if (!en) {
			local_state = PE_PAUSED;
			break;
		}

		exe_state(port, PE_OBJ(port), RUN_SIG);
		break;
	case PE_PAUSED:
		if (en)
			local_state = PE_INIT;
		break;
	}
}

void pe_pass_up_message(int port)
{
	pe[port].flags |= PE_FLAGS_MSG_RECEIVED;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void pe_hard_reset_sent(int port)
{
	/* Do nothing */
}

void pe_got_hard_reset(int port)
{
	/* Do nothing */
}

void pe_report_error(int port, enum pe_error e)
{
	/* Do nothing */
}

void pe_got_soft_reset(int port)
{
	/* Do nothing */
}

void pe_message_sent(int port)
{
	/* Do nothing */
}

static unsigned int pe_request(int port, enum signal sig)
{
	int ret;

	ret = (*pe_request_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int pe_request_entry(int port)
{
	return 0;
}

static unsigned int pe_request_run(int port)
{
	uint32_t *payload = (uint32_t *)emsg[port].buf;
	uint32_t header = emsg[port].header;
	uint32_t vdo = payload[0];

	if (pe[port].flags & PE_FLAGS_MSG_RECEIVED) {
		pe[port].flags &= ~PE_FLAGS_MSG_RECEIVED;

		/*
		 * Only support Structured VDM Discovery
		 * Identity message
		 */

		if (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF)
			return 0;

		if (PD_HEADER_CNT(header) == 0)
			return 0;

		if (!PD_VDO_SVDM(vdo))
			return 0;

		if (PD_VDO_CMD(vdo) != CMD_DISCOVER_IDENT)
			return 0;

#ifdef CONFIG_USB_TYPEC_CTVPD
		/*
		 * We have a valid DISCOVER IDENTITY message.
		 * Attempt to reset support timer
		 */
		tc_reset_support_timer(port);
#endif
		/* Prepare to send ACK */

		/* VDM Header */
		payload[0] = VDO(
			USB_VID_GOOGLE,
			1, /* Structured VDM */
			VDO_SVDM_VERS(1) |
			VDO_CMDT(CMDT_RSP_ACK) |
			CMD_DISCOVER_IDENT);

		/* ID Header VDO */
		payload[1] = VDO_IDH(
			0, /* Not a USB Host */
			1, /* Capable of being enumerated as USB Device */
			IDH_PTYPE_VPD,
			0, /* Modal Operation Not Supported */
			USB_VID_GOOGLE);

		/* Cert State VDO */
		payload[2] = 0;

		/* Product VDO */
		payload[3] = VDO_PRODUCT(
			CONFIG_USB_PID,
			USB_BCD_DEVICE);

		/* VPD VDO */
		payload[4] = VDO_VPD(
			VPD_HW_VERSION,
			VPD_FW_VERSION,
			VPD_MAX_VBUS_20V,
			VPD_VBUS_IMP(VPD_VBUS_IMPEDANCE),
			VPD_GND_IMP(VPD_GND_IMPEDANCE),
#ifdef CONFIG_USB_TYPEC_CTVPD
			VPD_CTS_SUPPORTED
#else
			VPD_CTS_NOT_SUPPORTED
#endif
		);

		/* 20 bytes, 5 data objects */
		emsg[port].len = 20;

		/* Set to highest revision supported by both ports. */
		prl_set_rev(port, (PD_HEADER_REV(header) > PD_REV30) ?
					PD_REV30 : PD_HEADER_REV(header));

		/* Send the ACK */
		prl_send_data_msg(port, TCPC_TX_SOP_PRIME,
					PD_DATA_VENDOR_DEF);
	}

	return 0;
}

static unsigned int do_nothing_exit(int port)
{
	return 0;
}

static unsigned int get_super_state(int port)
{
	return RUN_SUPER;
}

#endif /* __CROS_EC_USB_PE_CTVPD_H */
