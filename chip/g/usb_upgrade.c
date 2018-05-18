/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "common.h"
#include "console.h"
#include "consumer.h"
#include "extension.h"
#include "queue_policies.h"
#include "shared_mem.h"
#include "system.h"
#include "upgrade_fw.h"
#include "usb-stream.h"

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
 * The programmer reports results by putting the return value into the same
 * buffer where the block was passed in. This wrapper retrieves the
 * programmer's return value, and sends it back to the host. The return value
 * is usually one byte in size, the only exception is the connection
 * establishment phase where the return value is 16 bytes in size.
 *
 * In the end of the successful image transfer and programming, the host sends
 * the reset command, and the device reboots itself.
 */

struct consumer const upgrade_consumer;
struct usb_stream_config const usb_upgrade;

static struct queue const upgrade_to_usb = QUEUE_DIRECT(64, uint8_t,
						     null_producer,
						     usb_upgrade.consumer);
static struct queue const usb_to_upgrade = QUEUE_DIRECT(64, uint8_t,
						     usb_upgrade.producer,
						     upgrade_consumer);

USB_STREAM_CONFIG_FULL(usb_upgrade,
		       USB_IFACE_UPGRADE,
		       USB_CLASS_VENDOR_SPEC,
		       USB_SUBCLASS_GOOGLE_CR50,
		       USB_PROTOCOL_GOOGLE_CR50_NON_HC_FW_UPDATE,
		       USB_STR_UPGRADE_NAME,
		       USB_EP_UPGRADE,
		       USB_MAX_PACKET_SIZE,
		       USB_MAX_PACKET_SIZE,
		       usb_to_upgrade,
		       upgrade_to_usb)


/* The receiver can be in one of the states below. */
enum rx_state {
	rx_idle,	   /* Nothing happened yet. */
	rx_inside_block,   /* Assembling a block to pass to the programmer. */
	rx_outside_block,  /* Waiting for the next block to start or for the
			      reset command. */
};

enum rx_state rx_state_ = rx_idle;
static uint8_t *block_buffer;
static uint32_t block_size;
static uint32_t block_index;

/*
 * Verify that the contents of the USB rx queue is a valid transfer start
 * message from host, and if so - save its contents in the passed in
 * update_frame_header structure.
 */
static int valid_transfer_start(struct consumer const *consumer, size_t count,
				struct update_frame_header *pupfr)
{
	int i;

	/*
	 * Let's just make sure we drain the queue no matter what the contents
	 * are. This way they won't be in the way during next callback, even
	 * if these contents are not what's expected.
	 */
	i = count;
	while (i > 0) {
		QUEUE_REMOVE_UNITS(consumer->queue, pupfr,
				   MIN(i, sizeof(*pupfr)));
		i -= sizeof(*pupfr);
	}

	if (count != sizeof(struct update_frame_header)) {
		CPRINTS("FW update: wrong first block, size %d", count);
		return 0;
	}

	/* In the first block the payload (pupfr->cmd) must be all zeros. */
	for (i = 0; i < sizeof(pupfr->cmd); i++)
		if (((uint8_t *)&pupfr->cmd)[i])
			return 0;
	return 1;
}

static int try_vendor_command(struct consumer const *consumer, size_t count)
{
	struct update_frame_header ufh;
	struct update_frame_header *cmd_buffer;
	uint16_t *subcommand;
	size_t request_size;
	/*
	 * Should be enough for any vendor command/response. We'll generate an
	 * error if it is not.
	 */
	uint8_t subcommand_body[32];

	if (count < sizeof(ufh))
		return 0;	/* Too short to be a valid vendor command. */

	/*
	 * Let's copy off the queue the upgrade frame header, to see if this
	 * is a channeled vendor command.
	 */
	queue_peek_units(consumer->queue, &ufh, 0, sizeof(ufh));
	if (be32toh(ufh.cmd.block_base) != CONFIG_EXTENSION_COMMAND)
		return 0;

	if (be32toh(ufh.block_size) != count) {
		CPRINTS("%s: problem: block size and count mismatch (%d != %d)",
			__func__, be32toh(ufh.block_size), count);
		return 0;
	}

	if (shared_mem_acquire(count, (char **)&cmd_buffer)
	    != EC_SUCCESS) {
		CPRINTS("%s: problem: failed to allocate block of %d",
			__func__, count);
		return 0;
	}

	/* Get the entire command, don't remove it from the queue just yet. */
	queue_peek_units(consumer->queue, cmd_buffer, 0, count);

	/* Looks like this is a vendor command, let's verify it. */
	if (!usb_pdu_valid(&cmd_buffer->cmd,
			  count - offsetof(struct update_frame_header, cmd))) {
		/* Didn't verify */
		shared_mem_release(cmd_buffer);
		return 0;
	}

	/* Looks good; remove from the queue and process it. */
	queue_advance_head(consumer->queue, count);

	subcommand = (uint16_t *)(cmd_buffer + 1);
	request_size = count - sizeof(struct update_frame_header) -
		       sizeof(*subcommand);

	if (request_size > sizeof(subcommand_body)) {
		const uint8_t err = VENDOR_RC_REQUEST_TOO_BIG;

		CPRINTS("%s: payload too big (%d)", __func__, request_size);
		QUEUE_ADD_UNITS(&upgrade_to_usb, &err, 1);
	} else {
		uint32_t rv;
		struct vendor_cmd_params p = {
			.code = be16toh(*subcommand),
			.buffer = subcommand_body,
			.in_size = request_size,
			/*
			 * The return code normally put into the TPM response
			 * header is not present in the USB response. Vendor
			 * command return code is guaranteed to fit in a
			 * byte. Let's keep space for it in the front of the
			 * buffer.
			 */
			.out_size = sizeof(subcommand_body) - 1,
			.flags = VENDOR_CMD_FROM_USB
		};
		memcpy(subcommand_body, subcommand + 1, request_size);
		rv = extension_route_command(&p);
		/*
		 * Copy actual response, if any, one byte up, to free room for
		 * the return code.
		 */
		if (p.out_size)
			memmove(subcommand_body + 1, subcommand_body,
				p.out_size);
		subcommand_body[0] = rv;  /* We care about LSB only. */

		QUEUE_ADD_UNITS(&upgrade_to_usb, subcommand_body,
				p.out_size + 1);
	}

	shared_mem_release(cmd_buffer);
	return 1;
}

/*
 * When was last time a USB callback was called, in microseconds, free running
 * timer.
 */
static uint64_t prev_activity_timestamp;

/*
 * A flag indicating that at least one valid PDU containing flash update block
 * has been received in the current transfer session.
 */
static uint8_t  data_was_transferred;

/* Called to deal with data from the host */
static void upgrade_out_handler(struct consumer const *consumer, size_t count)
{
	struct update_frame_header upfr;
	size_t resp_size;
	uint8_t resp_value;
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
		 * The payload must be an update initiating PDU.
		 *
		 * The size of the response returned in the same buffer will
		 * exceed the received frame size; Let's make sure there is
		 * enough room for the response in the buffer.
		 */
		union {
			struct update_frame_header upfr;
			struct {
				uint32_t unused;
				struct first_response_pdu startup_resp;
			};
		} u;

		/* Check is this is a channeled TPM extension command. */
		if (try_vendor_command(consumer, count))
			return;

		if (!valid_transfer_start(consumer, count, &u.upfr)) {
			/*
			 * Something is wrong, this payload is not a valid
			 * update start PDU. Let'w indicate this by returning
			 * a single byte error code.
			 */
			resp_value = UPGRADE_GEN_ERROR;
			CPRINTS("%s:%d", __FILE__, __LINE__);
			QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value, 1);
			return;
		}

		CPRINTS("FW update: starting...");
		fw_upgrade_command_handler(&u.upfr.cmd, count -
					   offsetof(struct update_frame_header,
						    cmd),
					   &resp_size);

		if (!u.startup_resp.return_value) {
			rx_state_ = rx_outside_block;  /* We're in business. */
			data_was_transferred = 0;   /* No data received yet. */
		}

		/* Let the host know what upgrader had to say. */
		QUEUE_ADD_UNITS(&upgrade_to_usb, &u.startup_resp, resp_size);
		return;
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
			if (command == UPGRADE_DONE) {
				CPRINTS("FW update: done");

				if (data_was_transferred) {
					fw_upgrade_complete();
					data_was_transferred = 0;
				}

				resp_value = 0;
				QUEUE_ADD_UNITS(&upgrade_to_usb,
						&resp_value, 1);
				rx_state_ = rx_idle;
				return;
			}
		}

		/*
		 * At this point we expect a block start message. It is
		 * sizeof(upfr) bytes in size, but is not the transfer start
		 * message, which also is of that size AND has the command
		 * field of all zeros.
		 */
		if (valid_transfer_start(consumer, count, &upfr) ||
		    (count != sizeof(upfr))) {
			/*
			 * Instead of a block start message we received either
			 * a transfer start message or a chunk. We must have
			 * gotten out of sync with the host.
			 */
			resp_value = UPGRADE_GEN_ERROR;
			CPRINTS("%s:%d", __FILE__, __LINE__);
			QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value, 1);
			return;
		}

		/* Let's allocate a large enough buffer. */
		block_size = be32toh(upfr.block_size) -
			offsetof(struct update_frame_header, cmd);
		if (shared_mem_acquire(block_size, (char **)&block_buffer)
		    != EC_SUCCESS) {
			CPRINTS("FW update: error: failed to alloc %d bytes.",
				block_size);
			resp_value = UPGRADE_MALLOC_ERROR;
			QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value, 1);
			return;
		}

		/*
		 * Copy the rest of the message into the block buffer to pass
		 * to the upgrader.
		 */
		block_index = sizeof(upfr) -
			offsetof(struct update_frame_header, cmd);
		memcpy(block_buffer, &upfr.cmd, block_index);
		block_size -= block_index;
		rx_state_ = rx_inside_block;
		return;
	}

	/* Must be inside block. */
	QUEUE_REMOVE_UNITS(consumer->queue, block_buffer + block_index, count);
	block_index += count;
	block_size -= count;

	if (block_size) {
		if (count == sizeof(upfr)) {
			/*
			 * A block header size instead of chunk size message
			 * has been received. There must have been some packet
			 * loss and the host is restarting this block.
			 *
			 * Let's copy its contents into the header structure.
			 */
			memcpy(&upfr, block_buffer + block_index - count,
			       count);


			/* And re-allocate a large enough buffer. */
			shared_mem_release(block_buffer);
			block_size = be32toh(upfr.block_size) -
				offsetof(struct update_frame_header, cmd);
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
			 * to pass to the upgrader.
			 */
			block_index = sizeof(upfr) -
				offsetof(struct update_frame_header, cmd);
			memcpy(block_buffer, &upfr.cmd, block_index);
			block_size -= block_index;
		}
		return;	/* More to come. */
	}

	/*
	 * Ok, the entire block has been received and reassembled, pass it to
	 * the updater for verification and programming.
	 */
	fw_upgrade_command_handler(block_buffer, block_index, &resp_size);

	/*
	 * There was at least an attempt to program the flash, set the
	 * flag.
	 */
	data_was_transferred = 1;
	resp_value = block_buffer[0];
	QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value, sizeof(resp_value));
	rx_state_ = rx_outside_block;
	shared_mem_release(block_buffer);
	block_buffer = NULL;
}

struct consumer const upgrade_consumer = {
	.queue = &usb_to_upgrade,
	.ops   = &((struct consumer_ops const) {
		.written = upgrade_out_handler,
	}),
};
