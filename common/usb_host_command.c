/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "consumer.h"
#include "ec_commands.h"
#include "host_command.h"
#include "printf.h"
#include "queue_policies.h"
#include "system.h"
#include "usb-stream.h"
#include "usb_api.h"
#include "usb_hw.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_USB, outstr)
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, "USBHC: " format, ##args)

enum usbhc_state {
	/* Initial State - Ready to receive next request */
	USBHC_STATE_READY_TO_RX = 0,
	/* Receiving request */
	USBHC_STATE_RECEIVING,
	/* Processing request */
	USBHC_STATE_PROCESSING,
	/* Sending response */
	USBHC_STATE_SENDING,
	/* Received bad data */
	USBHC_STATE_RX_BAD,
} state;

struct consumer const hostcmd_consumer;
struct producer const hostcmd_producer;
struct usb_stream_config const usbhc_stream;

/* RX (Host->EC) queue */
static struct queue const usb_to_hostcmd =
	QUEUE_DIRECT(64, uint8_t, usbhc_stream.producer, hostcmd_consumer);
/* TX (EC->Host) queue */
static struct queue const hostcmd_to_usb =
	QUEUE_DIRECT(64, uint8_t, hostcmd_producer, usbhc_stream.consumer);

USB_STREAM_CONFIG_FULL(usbhc_stream, USB_IFACE_HOSTCMD, USB_CLASS_VENDOR_SPEC,
		       USB_SUBCLASS_GOOGLE_HOSTCMD, USB_PROTOCOL_GOOGLE_HOSTCMD,
		       USB_STR_HOSTCMD_NAME, USB_EP_HOSTCMD,
		       USB_MAX_PACKET_SIZE, USB_MAX_PACKET_SIZE, usb_to_hostcmd,
		       hostcmd_to_usb, 1, 0)

static uint8_t in_msg[USBHC_MAX_REQUEST_SIZE];
static uint8_t out_msg[USBHC_MAX_RESPONSE_SIZE];
static uint32_t out_size;
static struct host_packet usbhc_packet;
static struct ec_host_request *header = (struct ec_host_request *)in_msg;

static void usbhc_read(struct producer const *producer, size_t count)
{
	static uint32_t out_index;
	size_t len;

	len = MIN(producer->queue->buffer_units, out_size - out_index);
	len = MIN(count, len);

	/* If we're not sending, what's going on? */
	if (state != USBHC_STATE_SENDING)
		return;

	/* Put a piece of a response in the Tx queue. */
	QUEUE_ADD_UNITS(producer->queue, out_msg + out_index, len);
	out_index += len;

	if (out_index < out_size)
		/* More data to send. */
		return;

	if (IS_ENABLED(DEBUG))
		CPRINTS("Tx complete (%u bytes)", out_index);
	out_index = 0;
	state = USBHC_STATE_READY_TO_RX;
}

struct producer const hostcmd_producer = {
	.queue = &hostcmd_to_usb,
	.ops = &((struct producer_ops const){
		.read = usbhc_read,
	}),
};

/*
 * Called when a command handler finished execution and prepared a response.
 * It's supposed returns a response back to the host.
 */
static void usbhc_send_response_packet(struct host_packet *pkt)
{
	/*
	 * If we're not processing, then the AP has already terminated the
	 * transaction and won't be listening to a response.
	 */
	if (state != USBHC_STATE_PROCESSING)
		return;

	if (sizeof(out_msg) < pkt->response_size) {
		CPRINTS("Reponse size (%u) exceeds Tx buffer",
			pkt->response_size);
		return;
	}

	memcpy(out_msg, pkt->response, pkt->response_size);
	out_size = pkt->response_size;
	state = USBHC_STATE_SENDING;

	usbhc_read(&hostcmd_producer, hostcmd_to_usb.buffer_units);
}

/*
 * Pass a complete packet to the host command protocol subsystem.
 */
static void usbhc_process_packet(uint32_t pkt_size)
{
	usbhc_packet.send_response = usbhc_send_response_packet;
	usbhc_packet.request = in_msg;
	usbhc_packet.request_temp = NULL;
	usbhc_packet.request_max = sizeof(in_msg);
	usbhc_packet.request_size = pkt_size;

	usbhc_packet.response = out_msg;
	usbhc_packet.response_max = sizeof(out_msg);
	usbhc_packet.response_size = 0;
	usbhc_packet.driver_result = EC_RES_SUCCESS;

	host_packet_receive(&usbhc_packet);
}

/*
 * Called when usb-stream copies incoming data to the USBHC's Rx queue.
 */
static void usbhc_written(struct consumer const *consumer, size_t count)
{
	static uint32_t block_index;
	static uint32_t expected_size;
	static uint64_t prev_activity_timestamp;
	uint64_t delta_time;

	/* How much time since the previous USB callback? */
	delta_time = get_time().val - prev_activity_timestamp;
	prev_activity_timestamp += delta_time;

	/* If a session lasts more than 5 seconds, let's start over. */
	if ((delta_time > 5 * SECOND) && state != USBHC_STATE_READY_TO_RX) {
		state = USBHC_STATE_READY_TO_RX;
		CPRINTS("Recovering after timeout");
	}

	switch (state) {
	case USBHC_STATE_READY_TO_RX:
		if (IS_ENABLED(DEBUG))
			CPRINTS("Rx start. (count=%d)", count);
		block_index = 0;
		/* Only version 3 is supported. Using in_msg as a courtesy. */
		QUEUE_REMOVE_UNITS(consumer->queue, in_msg, count);
		if (IS_ENABLED(DEBUG)) {
			char str_buf[hex_str_buf_size(count)];

			snprintf_hex_buffer(str_buf, sizeof(str_buf),
					    HEX_BUF(in_msg, count));
			CPRINTS("%s", str_buf);
		}
		if (in_msg[0] != EC_HOST_REQUEST_VERSION) {
			CPRINTS("Unsupported version: %u", in_msg[0]);
			return;
		}
		block_index += count;
		expected_size = host_request_expected_size(header);
		if (block_index < expected_size) {
			state = USBHC_STATE_RECEIVING;
		} else if (sizeof(in_msg) < expected_size) {
			CPRINTS("Expected data size (%d) is too large",
				expected_size);
			state = USBHC_STATE_RX_BAD;
		} else {
			if (IS_ENABLED(DEBUG))
				CPRINTS("Rx complete (%d bytes)", block_index);
			state = USBHC_STATE_PROCESSING;
			usbhc_process_packet(block_index);
		}
		return;
	case USBHC_STATE_RECEIVING:
		/* Continue to receive the remaining data. */
		break;
	case USBHC_STATE_RX_BAD:
		/*
		 * Once we're in RX_BAD, we'll discard the incoming data for 5
		 * seconds. We don't want to be READY too soon because most
		 * likely more anomalous data will come, hopefully the host will
		 * fix the situation.
		 */
		queue_advance_head(consumer->queue, count);
		return;
	case USBHC_STATE_PROCESSING:
	case USBHC_STATE_SENDING:
		/*
		 * Take no action and return though we may have resource to
		 * receive a new request. Host will get a buffer full error or
		 * timeout.
		 */
		return;
	}

	/* Receive the remaining packet. */

	if (IS_ENABLED(DEBUG))
		CPRINTS("Received %d bytes", count);

	if (sizeof(in_msg) < block_index + count) {
		CPRINTS("Rx buffer overflow");
		state = USBHC_STATE_RX_BAD;
		return;
	}
	QUEUE_REMOVE_UNITS(consumer->queue, in_msg + block_index, count);
	block_index += count;

	if (block_index < expected_size)
		return; /* More to come. */

	if (IS_ENABLED(DEBUG))
		CPRINTS("Rx complete (%d bytes)", block_index);

	if (expected_size < block_index) {
		CPRINTS("Packet is larger than expected (%d)", expected_size);
		state = USBHC_STATE_RX_BAD;
		return;
	}

	/* Ok, the entire packet has been received and assembled. */
	state = USBHC_STATE_PROCESSING;
	usbhc_process_packet(block_index);
}

struct consumer const hostcmd_consumer = {
	.queue = &usb_to_hostcmd,
	.ops = &((struct consumer_ops const){
		.written = usbhc_written,
	}),
};

static enum ec_status
host_command_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions |= BIT(3);
	r->max_request_packet_size = USBHC_MAX_REQUEST_SIZE;
	r->max_response_packet_size = USBHC_MAX_RESPONSE_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, host_command_protocol_info,
		     EC_VER_MASK(0));
