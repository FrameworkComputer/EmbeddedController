/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "comm-host.h"
#include "ectool.h"
#include "misc_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* clang-format off */
const char cmd_pdc_trace_usage[] =
	"\n\tCollect USB PDC messages\n"
	"\t-h         Usage help\n"
	"\t-p <port>  collect on USB-C port <port>|all|none|on|off "
		"(default all)\n"
	"\t-s         send to stdout (default if no other destination)\n"
	"\t-w <file>  write to <file>";
/* clang-format on */

static void walk_entries(const uint8_t *data, size_t data_size,
			 bool with_stdout);

static FILE *pcap = NULL;

int cmd_pdc_trace(int argc, char *argv[])
{
	struct ec_params_pdc_trace_msg_enable ep;
	struct ec_response_pdc_trace_msg_enable er;

	struct ec_response_pdc_trace_msg_get_entries *gr =
		(ec_response_pdc_trace_msg_get_entries *)ec_inbuf;

	int rv;

	bool h_flag = false;
	const char *p_flag = NULL;
	bool s_flag = false;
	const char *w_flag = NULL;

	/*
	 * output traces to stdout unless another output destination
	 * has been requested.
	 */
	bool with_stdout = true;

	int pdc_port = EC_PDC_TRACE_MSG_PORT_ALL;

	int c;
	optind = 0; /* reset previous getopt */

	while ((c = getopt(argc, argv, "hp:sw:")) != -1) {
		switch (c) {
		case 'h':
			h_flag = true;
			break;

		case 'p':
			p_flag = optarg;
			break;

		case 's':
			s_flag = true;
			break;

		case 'w':
			w_flag = optarg;
			with_stdout = false;
			break;

		default:
			/* unexpected option */
			return -1;
		}
	}

	if (h_flag || optind != argc) {
		fprintf(stderr, "Usage:%s\n", cmd_pdc_trace_usage);
		return -1;
	}

	if (p_flag != NULL) {
		if (strcmp(p_flag, "all") == 0 || strcmp(p_flag, "on") == 0) {
			pdc_port = EC_PDC_TRACE_MSG_PORT_ALL;
		} else if (strcmp(p_flag, "none") == 0 ||
			   strcmp(p_flag, "off") == 0) {
			pdc_port = EC_PDC_TRACE_MSG_PORT_NONE;
		} else {
			char *s;

			pdc_port = strtol(p_flag, &s, 0);
			if ((s && *s != '\0') ||
			    (pdc_port < 0 || pdc_port > UINT8_MAX ||
			     pdc_port == EC_PDC_TRACE_MSG_PORT_ALL ||
			     pdc_port == EC_PDC_TRACE_MSG_PORT_NONE)) {
				fprintf(stderr, "Bad port number: %s\n",
					p_flag);
				return -1;
			}
		}
	}

	if (pdc_port == EC_PDC_TRACE_MSG_PORT_NONE) {
		ep.port = pdc_port;
		rv = ec_command(EC_CMD_PDC_TRACE_MSG_ENABLE, 0, &ep, sizeof(ep),
				&er, sizeof(er));
		if (rv < 0)
			return rv;
		return 0;
	}

	if (w_flag != NULL) {
		pcap = pdc_pcap_open(w_flag);
		if (pcap == NULL)
			return -1;
	}

	ep.port = pdc_port;
	rv = ec_command(EC_CMD_PDC_TRACE_MSG_ENABLE, 0, &ep, sizeof(ep), &er,
			sizeof(er));
	if (rv < 0) {
		pdc_pcap_close(pcap);
		return rv;
	}

	if (pdc_port == EC_PDC_TRACE_MSG_PORT_ALL) {
		printf("tracing all ports\n");
	} else {
		printf("tracing port C%u\n", pdc_port);
	}

	if (s_flag) {
		with_stdout = true;
	}

	while (1) {
		size_t payload_size;

		rv = ec_command(EC_CMD_PDC_TRACE_MSG_GET_ENTRIES, 0, NULL, 0,
				gr, ec_max_insize);
		if (rv < 0)
			break;

		payload_size = gr->pl_size;

		if (payload_size == 0) {
			if (pcap != NULL)
				fflush(pcap);

			usleep(100 * 1000); /* 100 ms */
			continue;
		}

		walk_entries(gr->payload, payload_size, with_stdout);
	}

	pdc_pcap_close(pcap);

	/*
	 * Turn off tracing.
	 */
	ep.port = EC_PDC_TRACE_MSG_PORT_NONE;
	rv = ec_command(EC_CMD_PDC_TRACE_MSG_ENABLE, 0, &ep, sizeof(ep), &er,
			sizeof(er));
	if (rv < 0)
		return rv;

	return rv;
}

/*
 * format pcap entry
 */

/*
 * PDC messages get a 5 byte header to provide additional context when
 * decoding:
 *
 *   byte 0: trace message sequence number
 *   byte 2: the Type-C port number
 *   byte 3: the direction of message (EC-RX vs. EC-TX)
 *   byte 4: message type for PDC chip type specific decoding
 *
 * This is essentially pdc_trace_msg_entry without the timestamp
 * since PCAP entries have their own timestamp field.
 */

struct pcap_pdc_trace_msg_header {
	uint16_t seq_num;
	uint8_t port_num;
	uint8_t direction;
	uint8_t msg_type;
} __packed;

BUILD_ASSERT(sizeof(struct pcap_pdc_trace_msg_header) == 5);

static size_t trace_to_pcap(uint8_t *pcap_buf, size_t pcap_buf_size,
			    const struct pdc_trace_msg_entry *e)
{
	size_t count;
	const struct pcap_pdc_trace_msg_header th = {
		.seq_num = e->seq_num,
		.port_num = e->port_num,
		.direction = e->direction,
		.msg_type = e->msg_type,
	};

	count = MIN(e->pdc_data_size, pcap_buf_size - sizeof(th));

	memcpy(pcap_buf, &th, sizeof(th));
	memcpy(&pcap_buf[sizeof(th)], e->pdc_data, count);

	return sizeof(th) + count;
}

/*
 * Walk the sequence of trace entries returned by the EC and send them
 * to all requested destinations.
 */
static void walk_entries(const uint8_t *const data, size_t data_size,
			 bool with_stdout)
{
	uint8_t pcap_buf[500];
	const struct pdc_trace_msg_entry *e;
	size_t consumed_bytes = 0;

	for (;;) {
		size_t e_size;

		if (consumed_bytes >= data_size)
			break;

		e = (struct pdc_trace_msg_entry *)(data + consumed_bytes);
		e_size = sizeof(*e);
		if (consumed_bytes + e_size > data_size) {
			fprintf(stderr,
				"entry header out of bounds (%zu+%zu) > %zu\n",
				consumed_bytes, e_size, data_size);
			break;
		}

		e_size += e->pdc_data_size;
		if (consumed_bytes + e_size > data_size) {
			fprintf(stderr, "entry out of bounds (%zu+%zu) > %zu\n",
				consumed_bytes, e_size, data_size);
			break;
		}

		if (with_stdout) {
			printf("SEQ:%04x PORT:%u %s {\nbytes %u:", e->seq_num,
			       e->port_num, e->direction ? "OUT" : "IN",
			       e->pdc_data_size);
			for (int i = 0; i < e->pdc_data_size; ++i)
				printf(" %02x", e->pdc_data[i]);
			printf("\n}\n");
		}

		if (pcap != NULL) {
			size_t cc =
				trace_to_pcap(pcap_buf, sizeof(pcap_buf), e);

			if (pcap != NULL) {
				struct timeval tv;

				tv.tv_sec = e->time32_us / 1000000;
				tv.tv_usec = e->time32_us % 1000000;

				pdc_pcap_append(pcap, tv, pcap_buf, cc);
			}
		}

		consumed_bytes += e_size;
	}
}
