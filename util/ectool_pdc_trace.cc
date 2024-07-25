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
		"(default all)";
/* clang-format on */

static void walk_entries(const uint8_t *data, size_t data_size,
			 bool with_stdout);

int cmd_pdc_trace(int argc, char *argv[])
{
	struct ec_params_pdc_trace_msg_enable ep;
	struct ec_response_pdc_trace_msg_enable er;

	struct ec_response_pdc_trace_msg_get_entries *gr =
		(ec_response_pdc_trace_msg_get_entries *)ec_inbuf;

	int rv;

	bool h_flag = false;
	const char *p_flag = NULL;

	/*
	 * output traces to stdout unless another output destination
	 * has been requested.
	 */
	bool with_stdout = true;

	int pdc_port = EC_PDC_TRACE_MSG_PORT_ALL;

	int c;
	optind = 0; /* reset previous getopt */

	while ((c = getopt(argc, argv, "hp:")) != -1) {
		switch (c) {
		case 'h':
			h_flag = true;
			break;

		case 'p':
			p_flag = optarg;
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

	ep.port = pdc_port;
	rv = ec_command(EC_CMD_PDC_TRACE_MSG_ENABLE, 0, &ep, sizeof(ep), &er,
			sizeof(er));
	if (rv < 0) {
		return rv;
	}

	if (pdc_port == EC_PDC_TRACE_MSG_PORT_ALL) {
		printf("tracing all ports\n");
	} else {
		printf("tracing port C%u\n", pdc_port);
	}

	while (1) {
		size_t payload_size;

		rv = ec_command(EC_CMD_PDC_TRACE_MSG_GET_ENTRIES, 0, NULL, 0,
				gr, ec_max_insize);
		if (rv < 0)
			break;

		payload_size = gr->pl_size;

		if (payload_size == 0) {
			usleep(100 * 1000); /* 100 ms */
			continue;
		}

		walk_entries(gr->payload, payload_size, with_stdout);
	}

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
 * Walk the sequence of trace entries returned by the EC and send them
 * to all requested destinations.
 */
static void walk_entries(const uint8_t *const data, size_t data_size,
			 bool with_stdout)
{
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

		consumed_bytes += e_size;
	}
}
