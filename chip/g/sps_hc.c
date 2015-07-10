/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "sps.h"
#include "util.h"

/*
 * This implements EC host commands over the SPI bus, using the Cr50 SPS (SPI
 * slave) controller.
 *
 * Host commands are communicated using software flow-control, because most of
 * the embedded controllers either aren't fast enough or don't have any support
 * for hardware flow-control.
 *
 * Every SPI transaction is bi-directional, so when the AP sends commands to
 * the EC, a default "dummy" byte is returned at the same time. The EC
 * preconfigures that default response byte to indicate its status (ready,
 * busy, waiting for more input, etc). Once the AP has sent a complete command
 * message, it continues clocking bytes to the EC (which the EC ignores) and
 * just looks at the response byte that comes back. Once the EC has parsed the
 * command and is ready to reply, it sends a "start of frame" byte, followed by
 * the actual response. The AP continues to read and ignore bytes from the EC
 * until it sees the start of frame byte, and then it knows that the EC's
 * response is starting with the next byte. Refer to include/ec_commands.h for
 * more details.
 */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SPI, outstr)
#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SPI, format, ## args)

/*
 * Incoming messages are collected here until they're ready to process. The
 * buffer will start with a struct ec_host_request, followed by whatever data
 * is sent by the master.
 */
#define RXBUF_MAX 512				/* chosen arbitrarily */
static uint8_t rxbuf[RXBUF_MAX];
static unsigned int rxbuf_count;		/* num bytes received */
static unsigned int rxbuf_needed;		/* num bytes we'd like */

/*
 * Outgoing messages are shoved in here. We need a preamble byte to mark the
 * start of the data stream before the data itself.
 */
#define TXBUF_MAX 512				/* chosen arbitrarily */
static uint8_t txbuf[1 + TXBUF_MAX];

static enum spi_state {
	/* SPI not enabled (initial state, and when chipset is off) */
	SPI_STATE_DISABLED = 0,

	/* Ready to receive next request */
	SPI_STATE_READY_TO_RX,

	/* Receiving request */
	SPI_STATE_RECEIVING_HEADER,
	SPI_STATE_RECEIVING_BODY,

	/* Processing request */
	SPI_STATE_PROCESSING,

	/* Sending response */
	SPI_STATE_SENDING,

	/*
	 * Received bad data - transaction started before we were ready, or
	 * packet header from the master didn't parse properly. Ignoring
	 * received data.
	 */
	SPI_STATE_RX_BAD,
} state;

/* If the chip select is disabled, don't transmit response */
static int discard_response;

/* Callback to send our response to the master */
static void cb_send_response_packet(struct host_packet *pkt)
{
	uint8_t *bufptr;
	size_t data_size = pkt->response_size;

	/* Chip Select was deasserted before we got here */
	if (discard_response) {
		discard_response = 0;
		state = SPI_STATE_READY_TO_RX;
		sps_tx_status(EC_SPI_RX_READY);
		return;
	}

	/*
	 * The pkt->response will start at txbuf[1], which means that
	 * pkt->response_size doesn't include the preamble byte. We need to
	 * initialize it and start sending from there.
	 */
	txbuf[0] = EC_SPI_FRAME_START;
	data_size++;
	bufptr = txbuf;
	state = SPI_STATE_SENDING;

	/* There's probably still a race condition somewhere... */
	while (data_size && state == SPI_STATE_SENDING) {
		size_t cnt = sps_transmit(bufptr, data_size);
		data_size -= cnt;
		bufptr += cnt;
	}

	/* Clock out the end-of-packet marker when we're done. */
	sps_tx_status(EC_SPI_PAST_END);
}

static int req_header_looks_good(const struct ec_host_request *req)
{
	return (req->struct_version == EC_HOST_REQUEST_VERSION) &&
		(req->reserved == 0) &&
		(sizeof(*req) + req->data_len <= RXBUF_MAX);
}

/* RX FIFO handler (runs in interrupt context) */
static void hc_rx_handler(uint8_t *data, size_t data_size, int cs_enabled)
{
	struct ec_host_request *req = (struct ec_host_request *)rxbuf;
	static struct host_packet rx_packet;
	size_t i;

	if (!cs_enabled) {
		if (state == SPI_STATE_PROCESSING) {
			/*
			 * A task is preparing a response, but the master has
			 * stopped caring. Set a flag so when the response is
			 * ready we'll just throw it away and reset everything.
			 */
			discard_response = 1;
			return;
		}

		/* Otherwise, just go back to waiting for new input */
		state = SPI_STATE_READY_TO_RX;
		sps_tx_status(EC_SPI_RX_READY);
		return;
	}

	/* No data == nothing to do */
	if (!data_size)
		return;

	switch (state) {
	case SPI_STATE_READY_TO_RX:
		/* Starting a new RX transaction. */
		rxbuf_count = 0;
		state = SPI_STATE_RECEIVING_HEADER;
		sps_tx_status(EC_SPI_RECEIVING);
		/* Need a header first (proto v3 only) */
		rxbuf_needed = sizeof(*req);
		break;

	case SPI_STATE_RECEIVING_HEADER:
	case SPI_STATE_RECEIVING_BODY:
		/* still gathering bytes */
		break;

	case SPI_STATE_DISABLED:
		/*
		 * The master started a transaction but we weren't ready for it.
		 * Tell it we weren't ready, and ignore the incoming data until
		 * the master gives up.
		 */
		CPRINTS("SPI not ready (in state %d)", state);
		sps_tx_status(EC_SPI_NOT_READY);
		state = SPI_STATE_RX_BAD;
		return;

	default:
		/* Anything else doesn't need us to look at the input */
		return;
	}

	/* Collect incoming bytes */
	if (rxbuf_count + data_size > RXBUF_MAX)
		goto spi_event_error;
	memcpy(rxbuf + rxbuf_count, data, data_size);
	rxbuf_count += data_size;

	/* Wait until we have enough */
	if (rxbuf_count < rxbuf_needed)
		return;

	switch (state) {
	case SPI_STATE_RECEIVING_HEADER:
		/* Got the header - is it okay? */
		if (!req_header_looks_good(req))
			goto spi_event_error;

		/* Yep, now need the body */
		state = SPI_STATE_RECEIVING_BODY;
		rxbuf_needed += req->data_len;

		/* Still need more bytes? */
		if (rxbuf_count < rxbuf_needed)
			return;

		/* FALLTHROUGH */

	case SPI_STATE_RECEIVING_BODY:
		/* We have all the data we need. */
		state = SPI_STATE_PROCESSING;
		sps_tx_status(EC_SPI_PROCESSING);

		/* Hand it off to the host command task for processing */
		rx_packet.send_response = cb_send_response_packet;
		rx_packet.request = rxbuf;
		rx_packet.request_temp = NULL;
		rx_packet.request_max = RXBUF_MAX;
		rx_packet.request_size = rxbuf_count;
		rx_packet.response = txbuf + 1;	/* skip preamble byte */
		rx_packet.response_max = TXBUF_MAX;
		rx_packet.response_size = 0;
		rx_packet.driver_result = EC_RES_SUCCESS;
		host_packet_receive(&rx_packet);
		break;

	default:
		/* shouldn't get here */
		goto spi_event_error;
	}

	return;

 spi_event_error:
	/* Error, timeout, or protocol we can't handle.  Ignore data. */
	sps_tx_status(EC_SPI_RX_BAD_DATA);
	state = SPI_STATE_RX_BAD;

	CPRINTS("SPI RX BAD DATA");
	CPRINTF("[rxbuf (%d): ", rxbuf_count);
	for (i = 0; i < rxbuf_count; i++)
		CPRINTF("%02x ", rxbuf[i]);
	CPRINTF("]\n");
}

static void sps_hc_enable(void)
{
	/* I'm not listening, la la la la la ... */
	sps_tx_status(EC_SPI_NOT_READY);

	/* We are no longer actively processing a transaction */
	state = SPI_STATE_DISABLED;

	/* Ready to receive */
	sps_register_rx_handler(hc_rx_handler);

	/* Here we go */
	discard_response = 0;
	state = SPI_STATE_READY_TO_RX;
	sps_tx_status(EC_SPI_RX_READY);
}
DECLARE_HOOK(HOOK_INIT, sps_hc_enable, HOOK_PRIO_DEFAULT);

static void sps_hc_disable(void)
{
	sps_unregister_rx_handler();

	/* We are no longer actively processing a transaction */
	state = SPI_STATE_DISABLED;
}

static int sps_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	r->protocol_versions = (1 << 3);
	r->max_request_packet_size = RXBUF_MAX;
	r->max_response_packet_size = TXBUF_MAX;
	r->flags = 0;

	args->response_size = sizeof(*r);
	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     sps_get_protocol_info,
		     EC_VER_MASK(0));

static int command_sps(int argc, char **argv)
{
	if (argc > 1) {
		if (0 != strcasecmp(argv[1], "off"))
			return EC_ERROR_PARAM1;

		sps_hc_disable();
		ccprintf("SPS host commands disabled\n");
		return EC_SUCCESS;
	}

	sps_hc_enable();
	ccprintf("SPS host commands enabled\n");
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(spshc, command_sps,
			"[off]",
			"Enable (default) or disable SPS host commands",
			NULL);
