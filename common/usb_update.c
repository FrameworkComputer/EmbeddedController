/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "common.h"
#include "console.h"
#include "consumer.h"
#include "include/compile_time_macros.h"
#include "queue_policies.h"
#include "shared_mem.h"
#include "system.h"
#include "update_fw.h"
#include "usb-stream.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

/*
 * This file is an adaptation layer between the USB interface and the firmware
 * update engine. The engine expects to receive long blocks of data, 1K or so
 * in size, prepended by the offset where the data needs to be programmed into
 * the flash and a 4 byte integrity check value.
 *
 * The USB transfer, on the other hand, operates on much shorter chunks of
 * data, typically 64 bytes in this case. This module reassembles firmware
 * programming blocks from the USB chunks, and invokes the programmer passing
 * it the full block.
 *
 * The programmer reports results by putting the return value of one or four
 * bytes into the same buffer where the block was passed in. This wrapper
 * retrieves the programmer's return value, normalizes it to 4 bytes and sends
 * it back to the host.
 *
 * In the end of the successful image transfer and programming, the host send
 * the reset command, and the device reboots itself.
 */

struct consumer const update_consumer;
struct usb_stream_config const usb_update;

static struct queue const update_to_usb = QUEUE_DIRECT(64, uint8_t,
						     null_producer,
						     usb_update.consumer);
static struct queue const usb_to_update = QUEUE_DIRECT(64, uint8_t,
						     usb_update.producer,
						     update_consumer);

USB_STREAM_CONFIG_FULL(usb_update,
		       USB_IFACE_UPDATE,
		       USB_CLASS_VENDOR_SPEC,
		       USB_SUBCLASS_GOOGLE_UPDATE,
		       USB_PROTOCOL_GOOGLE_UPDATE,
		       USB_STR_UPDATE_NAME,
		       USB_EP_UPDATE,
		       USB_MAX_PACKET_SIZE,
		       USB_MAX_PACKET_SIZE,
		       usb_to_update,
		       update_to_usb)


/* The receiver can be in one of the states below. */
enum rx_state {
	rx_idle,	   /* Nothing happened yet. */
	rx_inside_block,   /* Assembling a block to pass to the programmer. */
	rx_outside_block,  /* Waiting for the next block to start or for the
			      reset command. */
	rx_awaiting_reset /* Waiting for reset confirmation. */
};

/* This is the format of the header the programmer expects. */
struct update_command {
	uint32_t  block_digest;  /* first 4 bytes of sha1 of the rest of the
				    block. */
	uint32_t  block_base;    /* Offset of this block into the flash SPI. */
};

/* This is the format of the header the host uses. */
struct update_pdu_header {
	uint32_t block_size;    /* Total size of the block, including this
				   field. */
	union {
		struct update_command cmd;
		uint32_t resp; /* The programmer puts response to the same
				  buffer where the command was. */
	};
	/* The actual payload goes here. */
};

enum rx_state rx_state_ = rx_idle;
static uint8_t *block_buffer;
static uint32_t block_size;
static uint32_t block_index;

/*
 * Verify that the contens of the USB rx queue is a valid transfer start
 * message from host, and if so - save its contents in the passed in
 * update_pdu_header structure.
 */
static int valid_transfer_start(struct consumer const *consumer, size_t count,
				struct update_pdu_header *pupdu)
{
	int i;

	/*
	 * Let's just make sure we drain the queue no matter what the contents
	 * are. This way they won't be in the way during next callback, even
	 * if these contents are not what's expected.
	 */
	i = count;
	while (i > 0) {
		QUEUE_REMOVE_UNITS(consumer->queue, pupdu,
				   MIN(i, sizeof(*pupdu)));
		i -= sizeof(*pupdu);
	}

	if (count != sizeof(struct update_pdu_header)) {
		CPRINTS("FW update: wrong first block, size %d", count);
		return 0;
	}

	/* In the first block the payload (updu.cmd) must be all zeros. */
	for (i = 0; i < sizeof(pupdu->cmd); i++)
		if (((uint8_t *)&pupdu->cmd)[i])
			return 0;
	return 1;
}

/*
 * When was last time a USB callback was called, in microseconds, free running
 * timer.
 */
static uint64_t prev_activity_timestamp;

#define UPDATE_PROTOCOL_VERSION 2

/* Called to deal with data from the host */
static void update_out_handler(struct consumer const *consumer, size_t count)
{
	struct update_pdu_header updu;
	size_t resp_size;
	uint32_t resp_value;
	uint64_t delta_time;

	/* How much time since the previous USB callback? */
	delta_time = get_time().val - prev_activity_timestamp;
	prev_activity_timestamp += delta_time;

	/* If timeout exceeds 5 seconds - let's start over. */
	if ((delta_time > 5000000) && (rx_state_ != rx_idle)) {
		if (block_buffer) {
			/*
			 * Previous transfer could have been aborted mid
			 * block.
			 */
			shared_mem_release(block_buffer);
			block_buffer = NULL;
		}
		rx_state_ = rx_idle;
		CPRINTS("FW update: recovering after timeout");
	}

	if (rx_state_ == rx_idle) {
		/*
		 * When responding to the very first packet of the update
		 * sequence, the original implementation was responding with a
		 * four byte value, just as to any other block of the transfer
		 * sequence.
		 *
		 * It became clear that there is a need to be able to enhance
		 * the update protocol, while stayng backwards compatible. To
		 * achieve that we respond to the very first packet with an 8
		 * byte value, the first 4 bytes the same as before, the
		 * second 4 bytes - the protocol version number.
		 *
		 * This way if on the host side receiving of a four byte value
		 * in response to the first packet is an indication of the
		 * 'legacy' protocol, version 0. Receiving of an 8 byte
		 * response would communicate the protocol version in the
		 * second 4 bytes.
		 */
		struct {
			uint32_t value;
			uint32_t version;
		} startup_resp;

		if (!valid_transfer_start(consumer, count, &updu))
			return;

		CPRINTS("FW update: starting...");

		fw_update_command_handler(&updu.cmd, count -
					   offsetof(struct update_pdu_header,
						    cmd),
					   &resp_size);

		if (resp_size == 4) {
			/* Already in network order. */
			startup_resp.value = updu.resp;
			rx_state_ = rx_outside_block;
		} else {
			/* This must be a single byte error code. */
			startup_resp.value = htobe32(*((uint8_t *)&updu.resp));
		}

		startup_resp.version = htobe32(UPDATE_PROTOCOL_VERSION);

		/* Let the host know what updater had to say. */
		QUEUE_ADD_UNITS(&update_to_usb, &startup_resp,
				sizeof(startup_resp));
		return;
	}

	if (rx_state_ == rx_awaiting_reset) {
		/*
		 * Any USB data received in this state triggers reset, no
		 * response required.
		 */
		CPRINTS("reboot hard");
		cflush();
		system_reset(SYSTEM_RESET_HARD);
		while (1)
			;
	}

	if (rx_state_ == rx_outside_block) {
		/*
		 * Expecting to receive the beginning of the block or the
		 * reset command if all data blocks have been processed.
		 */
		if (count == 4) {
			uint32_t command;

			QUEUE_REMOVE_UNITS(consumer->queue, &command,
					   sizeof(command));
			command = be32toh(command);
			if (command == UPDATE_DONE) {
				CPRINTS("FW update: done");
				resp_value = 0;
				QUEUE_ADD_UNITS(&update_to_usb, &resp_value,
						sizeof(resp_value));
				rx_state_ = rx_awaiting_reset;
				return;
			} else {
				CPRINTS("Unexpected packet command 0x%x",
					command);
			}
		}

		/*
		 * At this point we expect a block start message. It is
		 * sizeof(updu) bytes in size, but is not the transfer start
		 * message, which also is of that size AND has the command
		 * field of all zeros.
		 */
		if (valid_transfer_start(consumer, count, &updu) ||
		    (count != sizeof(updu)))
			/*
			 * Instead of a block start message we received either
			 * a transfer start message or a chunk. We must have
			 * gotten out of sync with the host.
			 */
			return;

		/* Let's allocate a large enough buffer. */
		block_size = be32toh(updu.block_size) -
			offsetof(struct update_pdu_header, cmd);
		if (shared_mem_acquire(block_size, (char **)&block_buffer)
		    != EC_SUCCESS) {
			/* TODO:(vbendeb) report out of memory here. */
			CPRINTS("FW update: error: failed to alloc %d bytes.",
				block_size);
			return;
		}

		/*
		 * Copy the rest of the message into the block buffer to pass
		 * to the updater.
		 */
		block_index = sizeof(updu) -
			offsetof(struct update_pdu_header, cmd);
		memcpy(block_buffer, &updu.cmd, block_index);
		block_size -= block_index;
		rx_state_ = rx_inside_block;
		return;
	}

	/* Must be inside block. */
	QUEUE_REMOVE_UNITS(consumer->queue, block_buffer + block_index, count);
	block_index += count;
	block_size -= count;

	if (block_size) {
		if (count == sizeof(updu)) {
			/*
			 * A block header size instead of chunk size message
			 * has been received. There must have been some packet
			 * loss and the host is restarting this block.
			 *
			 * Let's copy its contents into the header structure.
			 */
			memcpy(&updu, block_buffer + block_index - count,
			       count);


			/* And re-allocate a large enough buffer. */
			shared_mem_release(block_buffer);
			block_size = be32toh(updu.block_size) -
				offsetof(struct update_pdu_header, cmd);
			if (shared_mem_acquire(block_size,
					       (char **)&block_buffer)
			    != EC_SUCCESS) {
				/* TODO:(vbendeb) report out of memory here. */
				CPRINTS("FW update: error: failed to alloc "
					"%d bytes.", block_size);
				return;
			}

			/*
			 * Copy the rest of the message into the block buffer
			 * to pass to the updater.
			 */
			block_index = sizeof(updu) -
				offsetof(struct update_pdu_header, cmd);
			memcpy(block_buffer, &updu.cmd, block_index);
			block_size -= block_index;
		}
		return;	/* More to come. */
	}

	/*
	 * Ok, the entire block has been received and reassembled, pass it to
	 * the updater for verification and programming.
	 */
	fw_update_command_handler(block_buffer, block_index, &resp_size);

	resp_value = block_buffer[0];
	QUEUE_ADD_UNITS(&update_to_usb, &resp_value, sizeof(resp_value));
	rx_state_ = rx_outside_block;
	shared_mem_release(block_buffer);
	block_buffer = NULL;
}

static void update_flush(struct consumer const *consumer)
{
}

struct consumer const update_consumer = {
	.queue = &usb_to_update,
	.ops   = &((struct consumer_ops const) {
		.written = update_out_handler,
		.flush   = update_flush,
	}),
};
