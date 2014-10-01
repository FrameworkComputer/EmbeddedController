/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PD module.
 */

#include "common.h"
#include "crc.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_pd_test_util.h"
#include "util.h"

struct pd_port_t {
	int host_mode;
	int cc_volt[2]; /* -1 for Hi-Z */
	int has_vbus;
	int msg_tx_id;
	int msg_rx_id;
	int polarity;
} pd_port[PD_PORT_COUNT];

/* Mock functions */

int pd_adc_read(int port, int cc)
{
	int val = pd_port[port].cc_volt[cc];
	if (val == -1)
		return pd_port[port].host_mode ? 3000 : 0;
	return val;
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

	for (i = 0; i < PD_PORT_COUNT; ++i) {
		pd_port[i].host_mode = 0;
		pd_port[i].cc_volt[0] = pd_port[i].cc_volt[1] = -1;
		pd_port[i].has_vbus = 0;
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

	pd_simulate_rx(port);
}

static void simulate_source_cap(int port)
{
	uint16_t header = PD_HEADER(PD_DATA_SOURCE_CAP, PD_ROLE_SOURCE,
				    pd_port[port].msg_rx_id, pd_src_pdo_cnt);
	simulate_rx_msg(port, header, pd_src_pdo_cnt, pd_src_pdo);
}

static void simulate_goodcrc(int port, int role, int id)
{
	simulate_rx_msg(port, PD_HEADER(PD_CTRL_GOOD_CRC, role, id, 0),
			0, NULL);
}

static int verify_goodcrc(int port, int role, int id)
{
	return pd_test_tx_msg_verify_sop(0) &&
	       pd_test_tx_msg_verify_short(0, PD_HEADER(PD_CTRL_GOOD_CRC,
							role, id, 0)) &&
	       pd_test_tx_msg_verify_crc(0) &&
	       pd_test_tx_msg_verify_eop(0);
}

static void plug_in_source(int port, int polarity)
{
	pd_port[port].has_vbus = 1;
	pd_port[port].cc_volt[polarity] = 3000;
}

static void plug_in_sink(int port, int polarity)
{
	pd_port[port].has_vbus = 0;
	pd_port[port].cc_volt[polarity] = 400; /* V_rd */
}

static void unplug(int port)
{
	pd_port[port].has_vbus = 0;
	pd_port[port].cc_volt[0] = -1;
	pd_port[port].cc_volt[1] = -1;
	task_wake(PORT_TO_TASK_ID(port));
	usleep(30 * MSEC);
}

static int test_request(void)
{
	plug_in_source(0, 0);
	task_wake(PORT_TO_TASK_ID(0));
	task_wait_event(100 * MSEC);
	TEST_ASSERT(pd_port[0].polarity == 0);

	/* We're in SNK_DISCOVERY now. Let's send the source cap. */
	simulate_source_cap(0);
	task_wait_event(30 * MSEC);
	TEST_ASSERT(verify_goodcrc(0, PD_ROLE_SINK, pd_port[0].msg_rx_id));

	/* Wait for the power request */
	task_wake(PORT_TO_TASK_ID(0));
	task_wait_event(35 * MSEC); /* tSenderResponse: 24~30 ms */
	inc_rx_id(0);

	/* Process the request */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(0));
	TEST_ASSERT(pd_test_tx_msg_verify_short(0,
			PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK,
				  pd_port[0].msg_tx_id, 1)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(0, RDO_FIXED(2, 450, 900, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(0));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(0));
	inc_tx_id(0);

	/* We're done */
	unplug(0);
	return EC_SUCCESS;
}

static int test_sink(void)
{
	int i;

	plug_in_sink(1, 1);
	task_wake(PORT_TO_TASK_ID(1));
	task_wait_event(250 * MSEC); /* tTypeCSinkWaitCap: 210~250 ms */
	TEST_ASSERT(pd_port[1].polarity == 1);

	/* The source cap should be sent */
	TEST_ASSERT(pd_test_tx_msg_verify_sop(1));
	TEST_ASSERT(pd_test_tx_msg_verify_short(1,
			PD_HEADER(PD_DATA_SOURCE_CAP, PD_ROLE_SOURCE,
				  pd_port[1].msg_tx_id, pd_src_pdo_cnt)));
	for (i = 0; i < pd_src_pdo_cnt; ++i)
		TEST_ASSERT(pd_test_tx_msg_verify_word(1, pd_src_pdo[i]));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(1));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(1));

	/* Looks good. Ack the source cap. */
	simulate_goodcrc(1, PD_ROLE_SINK, pd_port[1].msg_tx_id);
	task_wake(PORT_TO_TASK_ID(1));
	usleep(30 * MSEC);
	inc_tx_id(1);

	/* We're done */
	unplug(1);
	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();
	init_ports();
	pd_set_dual_role(PD_DRP_TOGGLE_ON);

	RUN_TEST(test_request);
	RUN_TEST(test_sink);

	test_print_result();
}
