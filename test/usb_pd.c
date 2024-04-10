/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PD module.
 */
#include "battery.h"
#include "common.h"
#include "crc.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_test_util.h"
#include "util.h"

#define PORT0 0
#define PORT1 1

#define BATTERY_DESIGN_VOLTAGE 7600
#define BATTERY_DESIGN_CAPACITY 5131
#define BATTERY_FULL_CHARGE_CAPACITY 5131
#define BATTERY_REMAINING_CAPACITY 2566

struct pd_port_t {
	int host_mode;
	int has_vbus;
	int msg_tx_id;
	int msg_rx_id;
	int polarity;
	int partner_role; /* -1 for none */
	int partner_polarity;
	int rev;
} pd_port[CONFIG_USB_PD_PORT_MAX_COUNT];

static int give_back_called;

/* Mock functions */
#ifdef CONFIG_USB_PD_REV30

uint16_t pd_get_identity_vid(int port)
{
	return 0;
}

uint16_t pd_get_identity_pid(int port)
{
	return 0;
}

int battery_status(int *status)
{
	*status = 1;
	return 0;
}

int battery_remaining_capacity(int *capacity)
{
	*capacity = BATTERY_REMAINING_CAPACITY;
	return 0;
}

int battery_full_charge_capacity(int *capacity)
{
	*capacity = BATTERY_FULL_CHARGE_CAPACITY;
	return 0;
}

int battery_design_capacity(int *capacity)
{
	*capacity = BATTERY_DESIGN_CAPACITY;
	return 0;
}

int battery_design_voltage(int *voltage)
{
	*voltage = BATTERY_DESIGN_VOLTAGE;
	return 0;
}

#endif

int pd_adc_read(int port, int cc)
{
	if (pd_port[port].host_mode &&
	    pd_port[port].partner_role == PD_ROLE_SINK)
		/* we are source connected to sink, return Rd/Open */
		return (pd_port[port].partner_polarity == cc) ? 400 : 3000;
	else if (!pd_port[port].host_mode &&
		 pd_port[port].partner_role == PD_ROLE_SOURCE)
		/* we are sink connected to source, return Rp/Open */
		return (pd_port[port].partner_polarity == cc) ? 1700 : 0;
	else if (pd_port[port].host_mode)
		/* no sink on the other side, both CC are opened */
		return 3000;
	else if (!pd_port[port].host_mode)
		/* no source on the other side, both CC are opened */
		return 0;

	/* should never get here */
	return 0;
}

int pd_snk_is_vbus_provided(int port)
{
	return pd_port[port].has_vbus;
}

void pd_set_host_mode(int port, int enable)
{
	pd_port[port].host_mode = enable;
}

void pd_select_polarity(int port, int polarity)
{
	pd_port[port].polarity = polarity;
}

int pd_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	return 0;
}

int board_select_rp_value(int port, int rp)
{
	return 0;
}

/* Tests */

void inc_tx_id(int port)
{
	pd_port[port].msg_tx_id = (pd_port[port].msg_tx_id + 1) % 7;
}

void inc_rx_id(int port)
{
	pd_port[port].msg_rx_id = (pd_port[port].msg_rx_id + 1) % 7;
}

static void init_ports(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); ++i) {
		pd_port[i].host_mode = 0;
		pd_port[i].partner_role = -1;
		pd_port[i].has_vbus = 0;
#ifdef CONFIG_USB_PD_REV30
		pd_port[i].rev = PD_REV30;
#else
		pd_port[i].rev = PD_REV20;
#endif
	}
}

static void simulate_rx_msg(int port, uint16_t header, int cnt,
			    const uint32_t *data)
{
	int i;

	pd_test_rx_set_preamble(port, 1);
	pd_test_rx_msg_append_sop(port);
	pd_test_rx_msg_append_short(port, header);

	crc32_init();
	crc32_hash16(header);
	for (i = 0; i < cnt; ++i) {
		pd_test_rx_msg_append_word(port, data[i]);
		crc32_hash32(data[i]);
	}
	pd_test_rx_msg_append_word(port, crc32_result());

	pd_test_rx_msg_append_eop(port);
	pd_test_rx_msg_append_last_edge(port);

	pd_simulate_rx(port);
}

static void simulate_wait(int port)
{
	uint16_t header = PD_HEADER(PD_CTRL_WAIT, PD_ROLE_SOURCE, PD_ROLE_DFP,
				    pd_port[port].msg_rx_id, 0,
				    pd_port[port].rev, 0);

	simulate_rx_msg(port, header, 0, NULL);
}

static void simulate_accept(int port)
{
	uint16_t header = PD_HEADER(PD_CTRL_ACCEPT, PD_ROLE_SOURCE, PD_ROLE_DFP,
				    pd_port[port].msg_rx_id, 0,
				    pd_port[port].rev, 0);

	simulate_rx_msg(port, header, 0, NULL);
}

static void simulate_reject(int port)
{
	uint16_t header = PD_HEADER(PD_CTRL_REJECT, PD_ROLE_SOURCE, PD_ROLE_DFP,
				    pd_port[port].msg_rx_id, 0,
				    pd_port[port].rev, 0);

	simulate_rx_msg(port, header, 0, NULL);
}

#ifdef CONFIG_USB_PD_REV30
static void simulate_get_bat_cap(int port)
{
	uint16_t msg[2];
	uint16_t header = PD_HEADER(PD_EXT_GET_BATTERY_CAP, PD_ROLE_SOURCE,
				    PD_ROLE_DFP, pd_port[port].msg_rx_id, 1,
				    pd_port[port].rev, 1);

	/* set extended header */
	msg[0] = PD_EXT_HEADER(0, 0, 1);

	/* set battery status ref */
	msg[1] = 0;

	simulate_rx_msg(port, header, 1, (const uint32_t *)msg);
}

static void simulate_get_bat_status(int port)
{
	uint16_t msg[2];
	uint16_t header = PD_HEADER(PD_EXT_GET_BATTERY_STATUS, PD_ROLE_SOURCE,
				    PD_ROLE_DFP, pd_port[port].msg_rx_id, 1,
				    pd_port[port].rev, 1);

	/* set extended header */
	msg[0] = PD_EXT_HEADER(0, 0, 1);

	/* set battery status ref */
	msg[1] = 0;

	simulate_rx_msg(port, header, 1, (const uint32_t *)msg);
}
#endif

static void simulate_source_cap(int port, uint32_t cnt)
{
	uint32_t src_pdo_cnt = (cnt == 0) ? 1 : pd_src_pdo_cnt;

	uint16_t header = PD_HEADER(PD_DATA_SOURCE_CAP, PD_ROLE_SOURCE,
				    PD_ROLE_DFP, pd_port[port].msg_rx_id,
				    src_pdo_cnt, pd_port[port].rev, 0);

	simulate_rx_msg(port, header, src_pdo_cnt, pd_src_pdo);
}

static void simulate_goodcrc(int port, int role, int id)
{
	simulate_rx_msg(port,
			PD_HEADER(PD_CTRL_GOOD_CRC, role, role, id, 0,
				  pd_port[port].rev, 0),
			0, NULL);
}

static int verify_goodcrc(int port, int role, int id)
{
	return pd_test_tx_msg_verify_sop(port) &&
	       pd_test_tx_msg_verify_short(port,
					   PD_HEADER(PD_CTRL_GOOD_CRC, role,
						     role, id, 0, 0, 0)) &&
	       pd_test_tx_msg_verify_crc(port) &&
	       pd_test_tx_msg_verify_eop(port);
}

static void plug_in_source(int port, int polarity)
{
	pd_port[port].has_vbus = 1;
	pd_port[port].partner_role = PD_ROLE_SOURCE;
	pd_port[port].partner_polarity = polarity;
	/* Indicate that the CC lines have changed. */
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC);
}

static void plug_in_sink(int port, int polarity)
{
	pd_port[port].has_vbus = 0;
	pd_port[port].partner_role = PD_ROLE_SINK;
	pd_port[port].partner_polarity = polarity;
	/* Indicate that the CC lines have changed. */
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC);
}

static void unplug(int port)
{
	pd_port[port].msg_tx_id = 0;
	pd_port[port].msg_rx_id = 0;
	pd_port[port].has_vbus = 0;
	pd_port[port].partner_role = -1;
	/* Indicate that the CC lines have changed. */
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC);
	task_wake(PD_PORT_TO_TASK_ID(port));
	crec_usleep(30 * MSEC);
}

void pd_snk_give_back(int port, uint32_t *const ma, uint32_t *const mv)
{
	if (*ma == 3000)
		give_back_called = 1;
}

static void simulate_ps_rdy(int port)
{
	uint16_t header = PD_HEADER(PD_CTRL_PS_RDY, PD_ROLE_SOURCE, PD_ROLE_DFP,
				    pd_port[port].msg_rx_id, 0,
				    pd_port[port].rev, 0);

	simulate_rx_msg(port, header, 0, NULL);
}

static void simulate_goto_min(int port)
{
	uint16_t header = PD_HEADER(PD_CTRL_GOTO_MIN, PD_ROLE_SOURCE,
				    PD_ROLE_DFP, pd_port[port].msg_rx_id, 0,
				    pd_port[port].rev, 0);

	simulate_rx_msg(port, header, 0, NULL);
}

static int test_request_with_wait_and_contract(void)
{
#ifdef CONFIG_USB_PD_REV30
	uint32_t expected_status_bsdo =
		BSDO_CAP(DIV_ROUND_NEAREST(BATTERY_REMAINING_CAPACITY *
						   BATTERY_DESIGN_VOLTAGE,
					   100000)) |
		BSDO_PRESENT;
	uint16_t expected_cap_hdr = PD_EXT_HEADER(0, 0, 9);
	uint16_t expected_cap_vid = USB_VID_GOOGLE;
#ifdef CONFIG_USB_PID
	uint16_t expected_cap_pid = CONFIG_USB_PID;
#else
	uint16_t expected_cap_pid = 0;
#endif
	uint16_t expected_cap_des = DIV_ROUND_NEAREST(
		BATTERY_DESIGN_CAPACITY * BATTERY_DESIGN_VOLTAGE, 100000);
	uint16_t expected_cap_ful = DIV_ROUND_NEAREST(
		BATTERY_FULL_CHARGE_CAPACITY * BATTERY_DESIGN_VOLTAGE, 100000);
	uint16_t expected_cap_type = 0;
#endif

#ifdef CONFIG_USB_PD_GIVE_BACK
	uint32_t expected_rdo =
		RDO_FIXED(2, 3000, PD_MIN_CURRENT_MA, RDO_GIVE_BACK);
#else
	uint32_t expected_rdo = RDO_FIXED(2, 3000, 3000, 0);
#endif
	uint8_t port = PORT0;

	plug_in_source(port, 0);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(2 * PD_T_CC_DEBOUNCE + 100 * MSEC);
	TEST_ASSERT(pd_port[port].polarity == 0);

	/* We're in SNK_DISCOVERY now. Let's send the source cap. */
	simulate_source_cap(port, 1);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're in SNK_REQUESTED. Send accept */
	simulate_accept(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/*
	 * We're in SNK_TRANSITION.
	 * And we have an explicit power contract.
	 */
	simulate_source_cap(port, 1);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're in SNK_REQUESTED. Send wait */
	simulate_wait(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	/* PD_T_SINK_REQUEST. Request is sent again after 100 ms */
	task_wait_event(100 * MSEC);
	inc_rx_id(port);

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* We had an explicit contract. So request should have been resent. */
	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're in SNK_REQUESTED. Send accept */
	simulate_accept(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/* We're in SNK_TRANSITION. Send ps_rdy */
	simulate_ps_rdy(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/*
	 * Test Extended Get_Battery_Cap and Get_Battery_Status messages.
	 */
#ifdef CONFIG_USB_PD_REV30
	/* We're in SNK_READY. Send get battery cap. */
	simulate_get_bat_cap(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_EXT_BATTERY_CAP, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 3, pd_port[port].rev, 1)));
	TEST_ASSERT(pd_test_tx_msg_verify_short(port, expected_cap_hdr));
	TEST_ASSERT(pd_test_tx_msg_verify_short(port, expected_cap_vid));
	TEST_ASSERT(pd_test_tx_msg_verify_short(port, expected_cap_pid));
	TEST_ASSERT(pd_test_tx_msg_verify_short(port, expected_cap_des));
	TEST_ASSERT(pd_test_tx_msg_verify_short(port, expected_cap_ful));
	TEST_ASSERT(pd_test_tx_msg_verify_short(port, expected_cap_type));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* Send get battery status. */
	simulate_get_bat_status(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_BATTERY_STATUS, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_status_bsdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);
#endif
	/* We're in SNK_READY. Send goto_min */
	simulate_goto_min(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

#ifdef CONFIG_USB_PD_GIVE_BACK
	TEST_ASSERT(give_back_called);
#else
	TEST_ASSERT(!give_back_called);
#endif
	/* We're done */
	unplug(port);

	return EC_SUCCESS;
}

static int test_request_with_wait(void)
{
#ifdef CONFIG_USB_PD_GIVE_BACK
	uint32_t expected_rdo = RDO_FIXED(1, 900, PD_MIN_CURRENT_MA,
					  RDO_CAP_MISMATCH | RDO_GIVE_BACK);
#else
	uint32_t expected_rdo = RDO_FIXED(1, 900, 900, RDO_CAP_MISMATCH);
#endif
	uint8_t port = PORT0;

	plug_in_source(port, 0);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(2 * PD_T_CC_DEBOUNCE + 100 * MSEC);
	TEST_ASSERT(pd_port[port].polarity == 0);

	/* We're in SNK_DISCOVERY now. Let's send the source cap. */
	simulate_source_cap(port, 0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request is good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(0));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're in SNK_REQUESTED. Send wait */
	simulate_wait(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/* We didn't have an explicit contract. So we're in SNK_DISCOVERY. */
	/* Resend Source Cap. */
	simulate_source_cap(port, 0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're done */
	unplug(port);
	return EC_SUCCESS;
}

static int test_request_with_wait_no_src_cap(void)
{
#ifdef CONFIG_USB_PD_GIVE_BACK
	uint32_t expected_rdo = RDO_FIXED(1, 900, PD_MIN_CURRENT_MA,
					  RDO_CAP_MISMATCH | RDO_GIVE_BACK);
#else
	uint32_t expected_rdo = RDO_FIXED(1, 900, 900, RDO_CAP_MISMATCH);
#endif
	uint8_t port = PORT0;

	plug_in_source(port, 0);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(2 * PD_T_CC_DEBOUNCE + 100 * MSEC);
	TEST_ASSERT(pd_port[port].polarity == 0);

	/* We're in SNK_DISCOVERY now. Let's send the source cap. */
	simulate_source_cap(port, 0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request is good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(0));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're in SNK_REQUESTED. Send wait */
	simulate_wait(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/*
	 * Some port partners do not send another SRC_CAP and expect us to send
	 * another REQUEST 100ms after the WAIT.
	 */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(100 * MSEC); /* tSinkRequest: 100 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're done */
	unplug(port);
	return EC_SUCCESS;
}

static int test_request_with_reject(void)
{
#ifdef CONFIG_USB_PD_GIVE_BACK
	uint32_t expected_rdo = RDO_FIXED(1, 900, PD_MIN_CURRENT_MA,
					  RDO_CAP_MISMATCH | RDO_GIVE_BACK);
#else
	uint32_t expected_rdo = RDO_FIXED(1, 900, 900, RDO_CAP_MISMATCH);
#endif
	uint8_t port = PORT0;

	plug_in_source(port, 0);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(2 * PD_T_CC_DEBOUNCE + 100 * MSEC);
	TEST_ASSERT(pd_port[port].polarity == 0);

	/* We're in SNK_DISCOVERY now. Let's send the source cap. */
	simulate_source_cap(port, 0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request is good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(0));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're in SNK_REQUESTED. Send reject */
	simulate_reject(port);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/* We're in SNK_READY. Send source cap. again. */
	simulate_source_cap(port, 0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	/* We're done */
	unplug(port);
	return EC_SUCCESS;
}

static int test_request(void)
{
#ifdef CONFIG_USB_PD_GIVE_BACK
	uint32_t expected_rdo = RDO_FIXED(1, 900, PD_MIN_CURRENT_MA,
					  RDO_CAP_MISMATCH | RDO_GIVE_BACK);
#else
	uint32_t expected_rdo = RDO_FIXED(1, 900, 900, RDO_CAP_MISMATCH);
#endif
	uint8_t port = PORT0;

	plug_in_source(port, 0);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(2 * PD_T_CC_DEBOUNCE + 100 * MSEC);
	TEST_ASSERT(pd_port[port].polarity == 0);

	/* We're in SNK_DISCOVERY now. Let's send the source cap. */
	simulate_source_cap(port, 0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	/* Wait for the power request */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(port);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
			  pd_port[port].msg_tx_id, 1, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_rdo));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Request was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/* We're done */
	unplug(port);

	return EC_SUCCESS;
}

static int test_sink(void)
{
	int i;
	uint8_t port = PORT1;

	plug_in_sink(port, 1);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(250 * MSEC); /* tTypeCSinkWaitCap: 210~250 ms */
	TEST_ASSERT(pd_port[port].polarity == 1);

	/* The source cap should be sent */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port, PD_HEADER(PD_DATA_SOURCE_CAP, PD_ROLE_SOURCE, PD_ROLE_DFP,
				pd_port[port].msg_tx_id, pd_src_pdo_cnt,
				pd_port[port].rev, 0)));

	for (i = 0; i < pd_src_pdo_cnt; ++i)
		TEST_ASSERT(pd_test_tx_msg_verify_word(port, pd_src_pdo[i]));

	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	/* Wake from pd_start_tx */
	task_wake(PD_PORT_TO_TASK_ID(port));
	crec_usleep(30 * MSEC);

	/* Looks good. Ack the source cap. */
	simulate_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_tx_id);

	/* Wake from pd_rx_start */
	task_wake(PD_PORT_TO_TASK_ID(port));
	crec_usleep(30 * MSEC);
	inc_tx_id(port);

	/* We're done */
	unplug(port);
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	init_ports();
	pd_set_dual_role(PORT0, PD_DRP_TOGGLE_ON);
	pd_set_dual_role(PORT1, PD_DRP_TOGGLE_ON);

	RUN_TEST(test_request);
	RUN_TEST(test_sink);
	RUN_TEST(test_request_with_wait);
	RUN_TEST(test_request_with_wait_no_src_cap);
	RUN_TEST(test_request_with_wait_and_contract);
	RUN_TEST(test_request_with_reject);

	test_print_result();
}
