/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PD module.
 */
#define HIDE_EC_STDLIB
#include "common.h"
#include "task.h"
#include "tcpm.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define TASK_EVENT_FUZZ TASK_EVENT_CUSTOM_BIT(0)

#define PORT0	0

static int mock_tcpm_init(int port) { return EC_SUCCESS; }
static int mock_tcpm_release(int port) { return EC_SUCCESS; }

static int mock_tcpm_select_rp_value(int port, int rp)
{
	return EC_SUCCESS;
}

static int mock_tcpm_set_cc(int port, int pull) { return EC_SUCCESS; }
static int mock_tcpm_set_polarity(int port, int polarity) { return EC_SUCCESS; }
static int mock_tcpm_set_vconn(int port, int enable) { return EC_SUCCESS; }
static int mock_tcpm_set_msg_header(int port,
			int power_role, int data_role) { return EC_SUCCESS; }
static int mock_tcpm_set_rx_enable(int port, int enable) { return EC_SUCCESS; }
static int mock_tcpm_transmit(int port, enum tcpm_transmit_type type,
		uint16_t header, const uint32_t *data) { return EC_SUCCESS; }
static void mock_tcpc_alert(int port) {}
static int mock_tcpci_get_chip_info(int port, int live,
		struct ec_response_pd_chip_info_v1 **info)
{
	return EC_ERROR_UNIMPLEMENTED;
}

#define MAX_TCPC_PAYLOAD 28

struct message {
	uint8_t cnt;
	uint16_t header;
	uint8_t payload[MAX_TCPC_PAYLOAD];
} __packed;

struct tcpc_state {
	enum tcpc_cc_voltage_status cc1, cc2;
	struct message message;
};

static struct tcpc_state mock_tcpc_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static int mock_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	*cc1 = mock_tcpc_state[port].cc1;
	*cc2 = mock_tcpc_state[port].cc2;

	return EC_SUCCESS;
}

static int pending;

int tcpm_has_pending_message(const int port)
{
	return pending;
}

int tcpm_dequeue_message(const int port, uint32_t *const payload,
			 int *const header)
{
	struct message *m = &mock_tcpc_state[port].message;

	ccprints("%s", __func__);

	/* Force a segfault, if no message is actually pending. */
	if (pending == 0)
		m = NULL;

	*header = m->header;

	/*
	 * This mirrors what tcpci.c:tcpm_dequeue_message does: always copy the
	 * whole payload to destination.
	 */
	memcpy(payload, m->payload, sizeof(m->payload));

	pending--;
	return EC_SUCCESS;
}

/* Note this method can be called from an interrupt context. */
int tcpm_enqueue_message(const int port)
{
	pending = 1;

	/* Wake PD task up so it can process incoming RX messages */
	task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE, 0);

	return EC_SUCCESS;
}

void tcpm_clear_pending_messages(int port) {}

static const struct tcpm_drv mock_tcpm_drv = {
	.init                   = &mock_tcpm_init,
	.release                = &mock_tcpm_release,
	.get_cc                 = &mock_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level         = &mock_tcpm_get_vbus_level,
#endif
	.select_rp_value        = &mock_tcpm_select_rp_value,
	.set_cc                 = &mock_tcpm_set_cc,
	.set_polarity           = &mock_tcpm_set_polarity,
	.set_vconn              = &mock_tcpm_set_vconn,
	.set_msg_header         = &mock_tcpm_set_msg_header,
	.set_rx_enable          = &mock_tcpm_set_rx_enable,
	/* The core calls tcpm_dequeue_message. */
	.get_message_raw        = NULL,
	.transmit               = &mock_tcpm_transmit,
	.tcpc_alert             = &mock_tcpc_alert,
	.get_chip_info          = &mock_tcpci_get_chip_info,
};

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpm_drv,
	},
	{
		.drv = &mock_tcpm_drv,
	}
};

static pthread_cond_t done_cond;
static pthread_mutex_t lock;

enum tcpc_cc_voltage_status next_cc1, next_cc2;
const int MAX_MESSAGES = 8;
static struct message messages[MAX_MESSAGES];

void run_test(void)
{
	uint8_t port = PORT0;
	int i;

	ccprints("Fuzzing task started");
	wait_for_task_started();

	while (1) {
		task_wait_event_mask(TASK_EVENT_FUZZ, -1);

		memset(&mock_tcpc_state[port],
			0, sizeof(mock_tcpc_state[port]));

		task_set_event(PD_PORT_TO_TASK_ID(port),
			PD_EVENT_TCPC_RESET, 0);
		task_wait_event(250 * MSEC);

		mock_tcpc_state[port].cc1 = next_cc1;
		mock_tcpc_state[port].cc2 = next_cc2;

		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
		task_wait_event(50 * MSEC);

		/* Fake RX messages, one by one. */
		for (i = 0; i < MAX_MESSAGES && messages[i].cnt; i++) {
			memcpy(&mock_tcpc_state[port].message, &messages[i],
				sizeof(messages[i]));

			tcpm_enqueue_message(port);
			task_wait_event(50 * MSEC);
		}

		pthread_cond_signal(&done_cond);
	}
}

int test_fuzz_one_input(const uint8_t *data, unsigned int size)
{
	int i;

	if (size < 1)
		return 0;

	next_cc1 = data[0] & 0x0f;
	next_cc2 = (data[0] & 0xf0) >> 4;
	data++; size--;

	memset(messages, 0, sizeof(messages));

	for (i = 0; i < MAX_MESSAGES && size > 0; i++) {
		int cnt = data[0];

		if (cnt < 3 || cnt > MAX_TCPC_PAYLOAD+3 || cnt > size) {
			/* Invalid count, or out of bounds. */
			return 0;
		}

		memcpy(&messages[i], data, cnt);

		data += cnt; size -= cnt;
	}

	if (size != 0) {
		/* Useless extra data in buffer, skip. */
		return 0;
	}

	task_set_event(TASK_ID_TEST_RUNNER, TASK_EVENT_FUZZ, 0);
	pthread_cond_wait(&done_cond, &lock);

	return 0;
}
