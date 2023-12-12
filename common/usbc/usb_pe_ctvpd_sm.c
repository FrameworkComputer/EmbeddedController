/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "task.h"
#include "usb_emsg.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "util.h"

/* USB Policy Engine Charge-Through VCONN Powered Device module */

/* Policy Engine Flags */
#define PE_FLAGS_MSG_RECEIVED BIT(0)

/**
 * This is the PE Port object that contains information needed to
 * implement a VCONN and Charge-Through VCONN Powered Device.
 */
static struct policy_engine {
	/* state machine context */
	struct sm_ctx ctx;
	/* port flags, see PE_FLAGS_* */
	uint32_t flags;
} pe[CONFIG_USB_PD_PORT_MAX_COUNT];

/* List of all policy-engine-level states */
enum usb_pe_state {
	PE_REQUEST,
};

/* Forward declare the full list of states. This is indexed by usb_pe_states */
static const struct usb_state pe_states[];

static void set_state_pe(const int port, enum usb_pe_state new_state)
{
	set_state(port, &pe[port].ctx, &pe_states[new_state]);
}

static void pe_init(int port)
{
	const struct sm_ctx cleared = {};

	pe[port].flags = 0;
	pe[port].ctx = cleared;
	set_state_pe(port, PE_REQUEST);
}

bool pe_in_frs_mode(int port)
{
	/* Will never be in FRS mode */
	return false;
}

bool pe_in_local_ams(int port)
{
	/* We never start a local AMS */
	return false;
}

void pe_run(int port, int evt, int en)
{
	static enum sm_local_state local_state[CONFIG_USB_PD_PORT_MAX_COUNT];

	switch (local_state[port]) {
	case SM_PAUSED:
		if (!en)
			break;
		__fallthrough;
	case SM_INIT:
		pe_init(port);
		local_state[port] = SM_RUN;
		__fallthrough;
	case SM_RUN:
		if (en)
			run_state(port, &pe[port].ctx);
		else
			local_state[port] = SM_PAUSED;
		break;
	}
}

void pe_message_received(int port)
{
	pe[port].flags |= PE_FLAGS_MSG_RECEIVED;
	task_wake(PD_PORT_TO_TASK_ID(port));
}

/**
 * NOTE:
 *	The Charge-Through Vconn Powered Device's Policy Engine is very
 *	simple and no implementation is needed for the following functions
 *	that might be called by the Protocol Layer.
 */

void pe_hard_reset_sent(int port)
{
	/* No implementation needed by this policy engine */
}

void pe_got_hard_reset(int port)
{
	/* No implementation needed by this policy engine */
}

void pe_report_error(int port, enum pe_error e, enum tcpci_msg_type type)
{
	/* No implementation needed by this policy engine */
}

void pe_report_discard(int port)
{
	/* No implementation needed by this policy engine */
}

void pe_got_soft_reset(int port)
{
	/* No implementation needed by this policy engine */
}

void pe_message_sent(int port)
{
	/* No implementation needed by this policy engine */
}

static void pe_request_run(const int port)
{
	uint32_t *payload = (uint32_t *)tx_emsg[port].buf;
	uint32_t header = rx_emsg[port].header;
	uint32_t vdo = *(uint32_t *)rx_emsg[port].buf;

	if (pe[port].flags & PE_FLAGS_MSG_RECEIVED) {
		pe[port].flags &= ~PE_FLAGS_MSG_RECEIVED;

		/*
		 * Only support Structured VDM Discovery
		 * Identity message
		 */

		if (PD_HEADER_TYPE(header) != PD_DATA_VENDOR_DEF)
			return;

		if (PD_HEADER_CNT(header) == 0)
			return;

		if (!PD_VDO_SVDM(vdo))
			return;

		if (PD_VDO_CMD(vdo) != CMD_DISCOVER_IDENT)
			return;

#ifdef CONFIG_USB_CTVPD
		/*
		 * We have a valid DISCOVER IDENTITY message.
		 * Attempt to reset support timer
		 */
		tc_reset_support_timer(port);
#endif
		/* Prepare to send ACK */

		/* VDM Header */
		payload[0] =
			VDO(USB_VID_GOOGLE, 1, /* Structured VDM */
			    VDO_SVDM_VERS_MAJOR(1) | VDO_CMDT(CMDT_RSP_ACK) |
				    CMD_DISCOVER_IDENT);

		/* ID Header VDO */
		payload[1] = VDO_IDH(0, /* Not a USB Host */
				     1, /* Capable of being enumerated as USB
					   Device */
				     IDH_PTYPE_VPD, 0, /* Modal Operation Not
							  Supported */
				     USB_VID_GOOGLE);

		/* Cert State VDO */
		payload[2] = 0;

		/* Product VDO */
		payload[3] = VDO_PRODUCT(CONFIG_USB_PID, USB_BCD_DEVICE);

		/* VPD VDO */
		payload[4] = VDO_VPD(
			VPD_HW_VERSION, VPD_FW_VERSION, VPD_MAX_VBUS_20V,
			IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_CT_CURRENT : 0,
			IS_ENABLED(CONFIG_USB_CTVPD) ?
				VPD_VBUS_IMP(VPD_VBUS_IMPEDANCE) :
				0,
			IS_ENABLED(CONFIG_USB_CTVPD) ?
				VPD_GND_IMP(VPD_GND_IMPEDANCE) :
				0,
			IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_CTS_SUPPORTED :
						       VPD_CTS_NOT_SUPPORTED);

		/* 20 bytes, 5 data objects */
		tx_emsg[port].len = 20;

		/* Set to highest revision supported by both ports. */
		prl_set_rev(port, TCPCI_MSG_SOP_PRIME,
			    (PD_HEADER_REV(header) > PD_REV30) ?
				    PD_REV30 :
				    PD_HEADER_REV(header));
		/* Send the ACK */
		prl_send_data_msg(port, TCPCI_MSG_SOP_PRIME,
				  PD_DATA_VENDOR_DEF);
	}
}

/* All policy-engine-level states. */
static const struct usb_state pe_states[] = {
	[PE_REQUEST] = {
		.run    = pe_request_run,
	},
};

#ifdef TEST_BUILD
const struct test_sm_data test_pe_sm_data[] = {
	{
		.base = pe_states,
		.size = ARRAY_SIZE(pe_states),
	},
};
const int test_pe_sm_data_size = ARRAY_SIZE(test_pe_sm_data);
#endif
