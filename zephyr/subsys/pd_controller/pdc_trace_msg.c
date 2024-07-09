/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB PDC message tracing.
 */

#include "atomic.h"
#include "builtin/assert.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/ring_buffer.h>

#include <drivers/pdc.h>

#define MSG_FIFO_SIZE CONFIG_USBC_PDC_TRACE_MSG_FIFO_SIZE

#define MSG_ENTRY_SEQ_NUM_BITS \
	(8 * member_size(struct pdc_trace_msg_entry, seq_num))
#define MSG_ENTRY_SEQ_NUM_MOD(n) ((n) & ((1 << MSG_ENTRY_SEQ_NUM_BITS) - 1))

BUILD_ASSERT(MSG_ENTRY_SEQ_NUM_BITS == 16);

LOG_MODULE_REGISTER(pdc_trace, CONFIG_USBC_PDC_TRACE_MSG_LOG_LEVEL);

static struct {
	uint32_t dropped;
	uint32_t seq_num;
} msg_fifo;

/*
 * Note: protected by msg_fifo.lock.
 */
static uint8_t pdc_trace_port = CONFIG_USBC_PDC_TRACE_MSG_PORT;
RING_BUF_DECLARE(msg_fifo_rbuf, MSG_FIFO_SIZE);
K_MUTEX_DEFINE(msg_fifo_mutex);

static void msg_fifo_lock(void)
{
	k_mutex_lock(&msg_fifo_mutex, K_FOREVER);
}

static void msg_fifo_unlock(void)
{
	k_mutex_unlock(&msg_fifo_mutex);
}

__test_only void pdc_trace_msg_fifo_reset(void)
{
	memset(&msg_fifo, 0, sizeof(msg_fifo));
	k_mutex_init(&msg_fifo_mutex);
	ring_buf_reset(&msg_fifo_rbuf);
}

__maybe_unused static bool is_port_present(int port)
{
	return (port >= 0) && (port < board_get_usb_pd_port_count());
}

/*
 * @brief push a PDC message into the FIFO
 *        a new trace entry is crated for the PDC message
 *        increment drop count if FIFO is full
 *
 * @param port      Port associated with the message
 * @param dir       Message is from transmit vs. receive path
 * @param msg_type  Token identifying message type
 * @param payload   Pointer to PDC message
 * @param msg_bytes Size of PDC message
 *
 * @return true IFF an entry was added to the FIFO
 */
static bool msg_fifo_push_entry(uint8_t port, uint8_t dir, uint8_t msg_type,
				const void *payload, int msg_bytes)
{
	size_t cap_entry_bytes;

	msg_fifo_lock();

	if (pdc_trace_port == EC_PDC_TRACE_MSG_PORT_NONE) {
		msg_fifo_unlock();
		return false;
	}

	if ((pdc_trace_port != EC_PDC_TRACE_MSG_PORT_ALL) &&
	    (pdc_trace_port != port)) {
		msg_fifo_unlock();
		return false;
	}

	struct pdc_trace_msg_entry e_header;

	cap_entry_bytes = sizeof(e_header) + msg_bytes;

	if (cap_entry_bytes > ring_buf_space_get(&msg_fifo_rbuf)) {
		/* FIFO overflow */
		LOG_DBG("%zu bytes > max %d bytes\n", cap_entry_bytes,
			(int)ring_buf_space_get(&msg_fifo_rbuf));
		++msg_fifo.dropped;
		msg_fifo_unlock();
		return false;
	}

	e_header.time32_us = get_time().le.lo;
	e_header.seq_num = msg_fifo.seq_num;
	e_header.port_num = port;
	e_header.direction = dir;
	e_header.msg_type = msg_type;
	e_header.pdc_data_size = msg_bytes;

	ring_buf_put(&msg_fifo_rbuf, (const uint8_t *)&e_header,
		     sizeof(e_header));
	ring_buf_put(&msg_fifo_rbuf, payload, e_header.pdc_data_size);

	msg_fifo.seq_num = MSG_ENTRY_SEQ_NUM_MOD(msg_fifo.seq_num + 1);

	msg_fifo_unlock();
	return true;
}

/*
 * @brief Control PDC message tracing on specified port.
 *
 * @param port Port number to change.
 *             Use EC_PDC_TRACE_MSG_PORT_NONE to disable.
 *             Use EC_PDC_TRACE_MSG_PORT_ALL to enable on all ports.
 *             Use valid port number to enable on a single port.
 *
 * @return previous port tracing value.
 */
test_export_static int pdc_trace_msg_enable(int new_port)
{
	int prev_port;

	msg_fifo_lock();

	prev_port = pdc_trace_port;
	pdc_trace_port = new_port;

	msg_fifo_unlock();

	return prev_port;
}

test_mockable bool pdc_trace_msg_req(int port,
				     enum pdc_trace_chip_type msg_type,
				     const uint8_t *buf, const int count)
{
	return msg_fifo_push_entry(port, PDC_TRACE_MSG_DIR_OUT, msg_type, buf,
				   count);
}

test_mockable bool pdc_trace_msg_resp(int port,
				      enum pdc_trace_chip_type msg_type,
				      const uint8_t *buf, const int count)
{
	return msg_fifo_push_entry(port, PDC_TRACE_MSG_DIR_IN, msg_type, buf,
				   count);
}

/*
 * @brief Convert bytes in msg_fifo to string notation in a buffer.
 *                   Bytes are not consumed.
 *
 * @param str        Buffer for string notation.
 * @param str_len    Size of buffer. The provided buffer must be
 *                   large enough for some useful data to be returned.
 * @param pl_size    Number of bytes to process.
 */
static void fifo_pl_to_str(char *str, const size_t str_len,
			   const uint8_t pl_size)
{
	int str_index;

	if (str_len < 20) {
		/* string buffer too small, give up */
		if (str_len > 0)
			str[0] = '\0';
		return;
	}

	str_index = sprintf(str, "bytes %u:", pl_size);

	/*
	 * figure out number of entries buffer can handle
	 */
	const int entry_str_len = 3;
	int entries;
	entries = (str_len - 1 - str_index) / entry_str_len;
	entries = MIN(entries, pl_size);

	uint32_t chunk1;
	uint32_t chunk2;
	uint8_t *p;

	chunk1 = ring_buf_get_claim(&msg_fifo_rbuf, &p, entries);

	for (int i = 0; i < chunk1; ++i) {
		snprintf(&str[str_index], entry_str_len + 1, " %02x", p[i]);
		str_index += entry_str_len;
	}

	if (chunk1 < entries) {
		chunk2 = ring_buf_get_claim(&msg_fifo_rbuf, &p,
					    entries - chunk1);
		for (int i = 0; i < chunk2; ++i) {
			snprintf(&str[str_index], entry_str_len + 1, " %02x",
				 p[i]);
			str_index += entry_str_len;
		}
	}

	ring_buf_get_finish(&msg_fifo_rbuf, 0);

	str[str_index] = '\0';
}

#define ENTRY_FMT "SEQ:%04x PORT:%u %s {\n%s\n}\n"
#define STR_BUF_SIZE 100

/*
 * @brief Print msg_entry and payload in FIFO to shell console or debug
 *                   log. Bytes are not consumed.
 *
 * @param sh         Shell handle for output.
 *                   If NULL, a debug log entry is written.
 * @param e          Pointer to msg_entry.
 */
__maybe_unused static void fifo_entry_print(const struct shell *sh,
					    const struct pdc_trace_msg_entry *e)
{
	char str_buf[STR_BUF_SIZE];
	uint16_t sn;
	uint8_t pn, dir, sz;

	sn = e->seq_num;
	pn = e->port_num;
	dir = e->direction;
	sz = e->pdc_data_size;

	fifo_pl_to_str(str_buf, sizeof(str_buf), sz);

	if (sh != NULL) {
		shell_fprintf(sh, SHELL_NORMAL, ENTRY_FMT, sn, pn,
			      dir ? "OUT" : "IN", str_buf);
	} else {
		LOG_DBG(ENTRY_FMT, sn, pn, dir ? "OUT" : "IN", str_buf);
	}
}

#ifdef CONFIG_USBC_PDC_TRACE_MSG_HOST_CMD

static enum ec_status
hc_pdc_trace_msg_enable(struct host_cmd_handler_args *args)
{
	const struct ec_params_pdc_trace_msg_enable *p = args->params;
	struct ec_response_pdc_trace_msg_enable *r = args->response;
	int req_port;

	req_port = p->port;

	switch (req_port) {
	case EC_PDC_TRACE_MSG_PORT_NONE:
	case EC_PDC_TRACE_MSG_PORT_ALL:
		break;
	default:
		if (!is_port_present(req_port))
			req_port = EC_PDC_TRACE_MSG_PORT_NONE;
	}

	memset(r, 0, sizeof(*r));
	r->port = pdc_trace_msg_enable(req_port);
	r->fifo_free = ring_buf_space_get(&msg_fifo_rbuf);
	r->dropped_count = msg_fifo.dropped;

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_PDC_TRACE_MSG_ENABLE, hc_pdc_trace_msg_enable,
		     EC_VER_MASK(0));

static enum ec_status
hc_pdc_trace_msg_get_entries(struct host_cmd_handler_args *args)
{
	struct ec_response_pdc_trace_msg_get_entries *r = args->response;

	struct pdc_trace_msg_entry entry;

	memset(r, 0, sizeof(*r));

	for (;;) {
		size_t cap_entry_bytes;
		uint32_t bytes;

		msg_fifo_lock();
		bytes = ring_buf_peek(&msg_fifo_rbuf, (uint8_t *)&entry,
				      sizeof(entry));

		if (bytes == 0) {
			/* FIFO empty */
			msg_fifo_unlock();
			break;
		}

		__ASSERT_NO_MSG(bytes >= sizeof(entry));

		cap_entry_bytes = sizeof(entry) + entry.pdc_data_size;

		if ((cap_entry_bytes <= MAX_HC_PDC_TRACE_MSG_GET_PAYLOAD) &&
		    (r->pl_size + cap_entry_bytes >
		     MAX_HC_PDC_TRACE_MSG_GET_PAYLOAD)) {
			/* not enough room, return next time */
			msg_fifo_unlock();
			break;
		}

		if (IS_ENABLED(CONFIG_USBC_PDC_TRACE_MSG_LOG_LEVEL_DBG)) {
			uint8_t *p;

			ring_buf_get_claim(&msg_fifo_rbuf, &p, sizeof(entry));
			fifo_entry_print(NULL, &entry);
			/*
			 * Note: No need for ring_buf_get_finish()
			 * here. fifo_entry_print() takes care of
			 * calling ring_buf_get_finish() after the
			 * entry, including payload, is processed.
			 */
		}

		if (cap_entry_bytes > MAX_HC_PDC_TRACE_MSG_GET_PAYLOAD) {
			/* this will never fit, skip it */
			ring_buf_get(&msg_fifo_rbuf, NULL, cap_entry_bytes);
			msg_fifo_unlock();
			continue;
		}

		ring_buf_get(&msg_fifo_rbuf, NULL, sizeof(entry));
		memcpy(&r->payload[r->pl_size], &entry, sizeof(entry));
		r->pl_size += sizeof(entry);

		ring_buf_get(&msg_fifo_rbuf, &r->payload[r->pl_size],
			     entry.pdc_data_size);
		msg_fifo_unlock();

		r->pl_size += entry.pdc_data_size;
	}

	args->response_size = sizeof(*r) + r->pl_size;

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_PDC_TRACE_MSG_GET_ENTRIES,
		     hc_pdc_trace_msg_get_entries, EC_VER_MASK(0));

#endif /* CONFIG_USBC_PDC_TRACE_MSG_HOST_CMD */

#ifdef CONFIG_USBC_PDC_TRACE_MSG_CONSOLE_CMD

#define PORT_NO_CHANGE -1

/*
 * @brief convert port number to string,
 *        handles PORT_NONE and PORT_ALL.
 *
 * @param port port number
 *
 * @return pointer to static string buffer
 */
static const char *port_num_str(uint8_t port)
{
	static char buf[4];

	switch (port) {
	case EC_PDC_TRACE_MSG_PORT_NONE:
		return "NONE";
	case EC_PDC_TRACE_MSG_PORT_ALL:
		return "ALL";
	default:
		sprintf(buf, "%u", port);
		return buf;
	}
}

int cmd_pdc_trace(const struct shell *sh, int argc, const char **argv)
{
	int port;
	char *rest;

	switch (argc) {
	case 1:
		port = PORT_NO_CHANGE;
		break;
	case 2:
		if (strcasecmp(argv[1], "on") == 0 ||
		    strcasecmp(argv[1], "all") == 0) {
			port = EC_PDC_TRACE_MSG_PORT_ALL;
			break;
		}
		if (strcasecmp(argv[1], "off") == 0 ||
		    strcasecmp(argv[1], "none") == 0) {
			port = EC_PDC_TRACE_MSG_PORT_NONE;
			break;
		}
		port = strtoi(argv[1], &rest, 0);
		if (*rest != '\0') {
			shell_error(sh, "Invalid port number: %s", argv[1]);
			return -ENOEXEC;
		}
		if (port < 0 || port == EC_PDC_TRACE_MSG_PORT_ALL ||
		    port == EC_PDC_TRACE_MSG_PORT_NONE) {
			shell_error(sh, "Port number out of range: %d", port);
			return -ENOEXEC;
		}
		break;
	default:
		return -ENOEXEC;
	}

	if (port == PORT_NO_CHANGE) {
		shell_fprintf(sh, SHELL_NORMAL, "PDC trace port is: %s\n",
			      port_num_str(pdc_trace_port));
	} else {
		int prev_port;

		prev_port = pdc_trace_msg_enable(port);

		shell_fprintf(sh, SHELL_NORMAL,
			      "PDC trace port changed from %s ",
			      port_num_str(prev_port));
		shell_fprintf(sh, SHELL_NORMAL, " to %s\n", port_num_str(port));
	}

	/*
	 * "off" (PDC_TRACE_MSG_PORT_NONE) only stops new entries.
	 * drain remaining messages.
	 */

	for (;;) {
		struct pdc_trace_msg_entry entry;
		uint32_t bytes;

		msg_fifo_lock();

		bytes = ring_buf_get(&msg_fifo_rbuf, (uint8_t *)&entry,
				     sizeof(entry));
		if (bytes == 0) {
			/* FIFO empty */
			msg_fifo_unlock();
			break;
		}

		fifo_entry_print(sh, &entry);
		ring_buf_get(&msg_fifo_rbuf, NULL, entry.pdc_data_size);

		msg_fifo_unlock();
	}

	shell_fprintf(sh, SHELL_NORMAL,
		      "msg_fifo: wr_available %u, dropped %u\n",
		      ring_buf_space_get(&msg_fifo_rbuf), msg_fifo.dropped);

	return 0;
}

#endif /* CONFIG_USBC_PDC_TRACE_MSG_CONSOLE_CMD */
