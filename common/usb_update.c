/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "common.h"
#include "console.h"
#include "consumer.h"
#include "extension.h"
#include "flash.h"
#include "queue_policies.h"
#include "host_command.h"
#include "rollback.h"
#include "rwsig.h"
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
 * The programmer reports results by putting the return value into the same
 * buffer where the block was passed in. This wrapper retrieves the
 * programmer's return value, and sends it back to the host. The return value
 * is usually one byte in size, the only exception is the connection
 * establishment phase where the return value is 16 bytes in size.
 *
 * In the end of the successful image transfer and programming, the host sends
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
};

enum rx_state rx_state_ = rx_idle;
static uint8_t block_buffer[sizeof(struct update_command) +
			    CONFIG_UPDATE_PDU_SIZE];
static uint32_t block_size;
static uint32_t block_index;

/*
 * Fetches a transfer start frame from the queue. This can be either an update
 * start frame (block_size = 0, all of cmd = 0), or the beginning of a frame
 * (block_size > 0, valid block_base in cmd).
 */
static int fetch_transfer_start(struct consumer const *consumer, size_t count,
				struct update_frame_header *pupfr)
{
	int i;

	/*
	 * Let's just make sure we drain the queue no matter what the contents
	 * are. This way they won't be in the way during next callback, even
	 * if these contents are not what's expected.
	 *
	 * Note: If count > sizeof(*pupfr), pupfr will be corrupted. This is
	 * ok as we will immediately fail after this.
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

	return 1;
}

static int try_vendor_command(struct consumer const *consumer, size_t count)
{
	char buffer[USB_MAX_PACKET_SIZE];
	struct update_frame_header *cmd_buffer = (void *)buffer;
	int rv = 0;

	/* Validate count (too short, or too long). */
	if (count < sizeof(*cmd_buffer) || count > sizeof(buffer))
		return 0;

	/*
	 * Let's copy off the queue the update frame header, to see if this
	 * is a channeled vendor command.
	 */
	queue_peek_units(consumer->queue, cmd_buffer, 0, sizeof(*cmd_buffer));
	if (be32toh(cmd_buffer->cmd.block_base) != UPDATE_EXTRA_CMD)
		return 0;

	if (be32toh(cmd_buffer->block_size) != count) {
		CPRINTS("%s: problem: block size and count mismatch (%d != %d)",
			__func__, be32toh(cmd_buffer->block_size), count);
		return 0;
	}

	/* Get the entire command, don't remove it from the queue just yet. */
	queue_peek_units(consumer->queue, cmd_buffer, 0, count);

	/* Looks like this is a vendor command, let's verify it. */
	if (update_pdu_valid(&cmd_buffer->cmd,
			  count - offsetof(struct update_frame_header, cmd))) {
		enum update_extra_command subcommand;
		uint8_t response;
		size_t response_size = sizeof(response);

		/* looks good, let's process it. */
		rv = 1;

		/* Now remove it from the queue. */
		queue_advance_head(consumer->queue, count);

		subcommand = be16toh(*((uint16_t *)(cmd_buffer + 1)));

		switch (subcommand) {
		case UPDATE_EXTRA_CMD_IMMEDIATE_RESET:
			CPRINTS("Rebooting!\n\n\n");
			cflush();
			system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED);
			/* Unreachable, unless something bad happens. */
			response = EC_RES_ERROR;
			break;
		case UPDATE_EXTRA_CMD_JUMP_TO_RW:
#ifdef CONFIG_RWSIG
			/*
			 * Tell rwsig task to jump to RW. This does nothing if
			 * verification failed, and will only jump later on if
			 * verification is still in progress.
			 */
			rwsig_continue();

			switch (rwsig_get_status()) {
			case RWSIG_VALID:
				response = EC_RES_SUCCESS;
				break;
			case RWSIG_INVALID:
				response = EC_RES_INVALID_CHECKSUM;
				break;
			case RWSIG_IN_PROGRESS:
				response = EC_RES_IN_PROGRESS;
				break;
			default:
				response = EC_RES_ERROR;
			}
#else
			system_run_image_copy(SYSTEM_IMAGE_RW);
#endif
			break;
#ifdef CONFIG_RWSIG
		case UPDATE_EXTRA_CMD_STAY_IN_RO:
			rwsig_abort();
			response = EC_RES_SUCCESS;
			break;
#endif
		case UPDATE_EXTRA_CMD_UNLOCK_RW:
			flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, 0);
			response = EC_RES_SUCCESS;
			break;
#ifdef CONFIG_ROLLBACK
		case UPDATE_EXTRA_CMD_UNLOCK_ROLLBACK:
			flash_set_protect(EC_FLASH_PROTECT_ROLLBACK_AT_BOOT, 0);
			response = EC_RES_SUCCESS;
			break;
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
#ifdef CONFIG_ROLLBACK_UPDATE
		case UPDATE_EXTRA_CMD_INJECT_ENTROPY: {
			/*
			 * Check that we are provided enough data (header +
			 * 2 bytes subcommand + secret length).
			 */
			int header_size = sizeof(*cmd_buffer) + 2;
			int entropy_count = count - header_size;

			if (entropy_count < CONFIG_ROLLBACK_SECRET_SIZE) {
				CPRINTS("Entropy too short");
				response = EC_RES_INVALID_PARAM;
				break;
			}

			CPRINTS("Adding %db of entropy", entropy_count);
			/* Add the entropy to secret. */
			rollback_add_entropy(buffer + header_size,
					     entropy_count);
			break;
		}
#endif
#endif /* CONFIG_ROLLBACK_SECRET_SIZE */
#endif /* CONFIG_ROLLBACK */
		default:
			response = EC_RES_INVALID_COMMAND;
		}

		QUEUE_ADD_UNITS(&update_to_usb, &response, response_size);
	}

	return rv;
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

/* Reply with an error to remote side, reset state. */
static void send_error_reset(uint8_t resp_value)
{
	QUEUE_ADD_UNITS(&update_to_usb, &resp_value, 1);
	rx_state_ = rx_idle;
	data_was_transferred = 0;
}

/* Called to deal with data from the host */
static void update_out_handler(struct consumer const *consumer, size_t count)
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

		/*
		 * An update start PDU is a command without any payload, with
		 * digest = 0, and base = 0.
		 */
		if (!fetch_transfer_start(consumer, count, &u.upfr) ||
			be32toh(u.upfr.block_size) !=
					sizeof(struct update_frame_header) ||
				u.upfr.cmd.block_digest != 0 ||
				u.upfr.cmd.block_base != 0) {
			/*
			 * Something is wrong, this payload is not a valid
			 * update start PDU. Let'w indicate this by returning
			 * a single byte error code.
			 */
			CPRINTS("FW update: invalid start.");
			send_error_reset(UPDATE_GEN_ERROR);
			return;
		}

		CPRINTS("FW update: starting...");
		fw_update_command_handler(&u.upfr.cmd, count -
					   offsetof(struct update_frame_header,
						    cmd),
					   &resp_size);

		if (!u.startup_resp.return_value) {
			rx_state_ = rx_outside_block;  /* We're in business. */
			data_was_transferred = 0;   /* No data received yet. */
		}

		/* Let the host know what updater had to say. */
		QUEUE_ADD_UNITS(&update_to_usb, &u.startup_resp, resp_size);
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
			if (command == UPDATE_DONE) {
				CPRINTS("FW update: done");

				if (data_was_transferred) {
					fw_update_complete();
					data_was_transferred = 0;
				}

				resp_value = 0;
				QUEUE_ADD_UNITS(&update_to_usb,
						&resp_value, 1);
				rx_state_ = rx_idle;
				return;
			}
		}

		/*
		 * At this point we expect a block start message. It is
		 * sizeof(upfr) bytes in size.
		 */
		if (!fetch_transfer_start(consumer, count, &upfr)) {
			CPRINTS("Invalid block start.");
			send_error_reset(UPDATE_GEN_ERROR);
			return;
		}

		/* Let's allocate a large enough buffer. */
		block_size = be32toh(upfr.block_size) -
			offsetof(struct update_frame_header, cmd);

		/*
		 * Only update start PDU is allowed to have a size 0 payload.
		 */
		if (block_size <= sizeof(struct update_command) ||
		    block_size > sizeof(block_buffer)) {
			CPRINTS("Invalid block size (%d).", block_size);
			send_error_reset(UPDATE_GEN_ERROR);
			return;
		}

		/*
		 * Copy the rest of the message into the block buffer to pass
		 * to the updater.
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
		if (count <= sizeof(upfr)) {
			/*
			 * A block header size instead of chunk size message
			 * has been received, let's abort the transfer.
			 */
			CPRINTS("Unexpected header");
			send_error_reset(UPDATE_GEN_ERROR);
			return;
		}
		return;	/* More to come. */
	}

	/*
	 * Ok, the entire block has been received and reassembled, pass it to
	 * the updater for verification and programming.
	 */
	fw_update_command_handler(block_buffer, block_index, &resp_size);

	/*
	 * There was at least an attempt to program the flash, set the
	 * flag.
	 */
	data_was_transferred = 1;
	resp_value = block_buffer[0];
	QUEUE_ADD_UNITS(&update_to_usb, &resp_value, sizeof(resp_value));
	rx_state_ = rx_outside_block;
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
