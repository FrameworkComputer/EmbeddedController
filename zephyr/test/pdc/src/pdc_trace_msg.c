/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "pdc_trace_msg.h"
#include "util_pcap.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>

#include <drivers/pdc.h>

/*
 * PDC messages are encapsulated in a link header when written to PCAP
 * so consumers have the necessary context info to decode the messages.
 */

struct pdc_trace_header {
	uint16_t seq_num;
	uint8_t port_num;
	uint8_t direction;
	uint8_t msg_type;
} __packed;

BUILD_ASSERT(sizeof(struct pdc_trace_header) == 5);

#define LINK_RX 0
#define LINK_TX 1

#define TRACE_PORT 0

FAKE_VALUE_FUNC(bool, pdc_trace_msg_req, int, enum pdc_trace_chip_type,
		const uint8_t *, const int);
FAKE_VALUE_FUNC(bool, pdc_trace_msg_resp, int, enum pdc_trace_chip_type,
		const uint8_t *, const int);

LOG_MODULE_REGISTER(pdc_trace, LOG_LEVEL_INF);

static uint8_t pcap_buf[500];
static uint16_t msg_seq_num;

BUILD_ASSERT(sizeof(msg_seq_num) ==
	     member_size(struct pdc_trace_header, seq_num));

static void pcap_out(const uint8_t *pcap_buf, size_t buf_len)
{
	static FILE *pcap;

	if (pcap == NULL)
		pcap = pcap_open();

	if (pcap != NULL)
		pcap_append(pcap, pcap_buf, buf_len);
}

static bool mock_pdc_trace_msg_req(int port, enum pdc_trace_chip_type msg_type,
				   const uint8_t *buf, const int count)
{
	int pl_size;

	if (port != TRACE_PORT)
		return false;

	if (count <= 0)
		return false;

	LOG_INF("PDC request: port %d, length %d:", port, count);
	LOG_HEXDUMP_INF(buf, count, "message:");

	const struct pdc_trace_header th = {
		.seq_num = msg_seq_num,
		.port_num = TRACE_PORT,
		.direction = LINK_TX,
		.msg_type = msg_type,
	};

	memcpy(pcap_buf, &th, sizeof(th));
	pl_size = MIN(count, sizeof(pcap_buf) - sizeof(th));
	memcpy(&pcap_buf[sizeof(th)], buf, pl_size);

	++msg_seq_num;

	pcap_out(pcap_buf, sizeof(th) + pl_size);

	return true;
}

static bool mock_pdc_trace_msg_resp(int port, enum pdc_trace_chip_type msg_type,
				    const uint8_t *buf, const int count)
{
	int pl_size;

	if (port != TRACE_PORT)
		return false;

	if (count <= 0)
		return false;

	LOG_INF("PDC response: port %d, length %d:", port, count);
	LOG_HEXDUMP_INF(buf, count, "message:");

	const struct pdc_trace_header th = {
		.seq_num = msg_seq_num,
		.port_num = TRACE_PORT,
		.direction = LINK_RX,
		.msg_type = msg_type,
	};
	memcpy(pcap_buf, &th, sizeof(th));
	pl_size = MIN(count, sizeof(pcap_buf) - sizeof(th));
	memcpy(&pcap_buf[sizeof(th)], buf, pl_size);

	++msg_seq_num;

	pcap_out(pcap_buf, sizeof(th) + pl_size);

	return true;
}

void set_pdc_trace_msg_mocks(void)
{
	pdc_trace_msg_req_fake.custom_fake = mock_pdc_trace_msg_req;
	pdc_trace_msg_resp_fake.custom_fake = mock_pdc_trace_msg_resp;
}
