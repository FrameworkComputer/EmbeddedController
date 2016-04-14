/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "common.h"
#include "console.h"
#include "consumer.h"
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
 * The programmer reports results by putting the return value of one or four
 * bytes into the same buffer where the block was passed in. This wrapper
 * retrieves the programmer's return value, normalizes it to 4 bytes and sends
 * it back to the host.
 *
 * In the end of the successful image transfer and programming, the host send
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
	rx_awaiting_reset /* Waiting for reset confirmation. */
};

/* This is the format of the header the programmer expects. */
struct upgrade_command {
	uint32_t  block_digest;  /* first 4 bytes of sha1 of the rest of the
				    block. */
	uint32_t  block_base;    /* Offset of this block into the flash SPI. */
};

/* This is the format of the header the host uses. */
struct update_pdu_header {
	uint32_t block_size;    /* Total size of the block, including this
				   field. */
	union {
		struct upgrade_command cmd;
		uint32_t resp; /* The programmer puts response to the same
				  buffer where the command was. */
	};
	/* The actual payload goes here. */
};

enum rx_state rx_state_ = rx_idle;
static uint8_t *block_buffer;
static uint32_t block_size;
static uint32_t block_index;

/* Called to deal with data from the host */
static void upgrade_out_handler(struct consumer const *consumer, size_t count)
{
	struct update_pdu_header updu;
	size_t resp_size;
	uint32_t resp_value;

	if (rx_state_ == rx_idle) {
		/* This better be the first block, of zero size. */
		if (count != sizeof(struct update_pdu_header)) {
			CPRINTS("FW update: wrong first block size %d\n",
				count);
			return;
		}
		QUEUE_REMOVE_UNITS(consumer->queue, &updu, count);

		CPRINTS("FW update: starting...\n");

		fw_upgrade_command_handler(&updu.cmd, count -
					   offsetof(struct update_pdu_header,
						    cmd),
					   &resp_size);

		if (resp_size == 4) {
			resp_value = updu.resp; /* Already in network order. */
			rx_state_ = rx_outside_block;
		} else {
			/* This must be a single byte error code. */
			resp_value = htobe32(*((uint8_t *)&updu.resp));
		}
		/* Let the host know what upgrader had to say. */
		QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value,
				sizeof(resp_value));
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
			if (command == UPGRADE_DONE) {
				CPRINTS("FW update: done\n");
				resp_value = 0;
				QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value,
						sizeof(resp_value));
				rx_state_ = rx_awaiting_reset;
				return;
			}
		}

		if (count < sizeof(updu)) {
			CPRINTS("FW update: error: first chunk of %d bytes\n",
				count);
			rx_state_ = rx_idle;
			return;
		}

		QUEUE_REMOVE_UNITS(consumer->queue, &updu, sizeof(updu));

		/* Let's allocate a large enough buffer. */
		block_size = be32toh(updu.block_size) -
			offsetof(struct update_pdu_header, cmd);
		if (shared_mem_acquire(block_size, (char **)&block_buffer)
		    != EC_SUCCESS) {
			/* TODO:(vbendeb) report out of memory here. */
			CPRINTS("FW update: error: failed to alloc %d bytes.\n",
				block_size);
			return;
		}

		/*
		 * Copy the rest of the message into the block buffer to pass
		 * to the upgrader.
		 */
		block_index = sizeof(updu) -
			offsetof(struct update_pdu_header, cmd);
		memcpy(block_buffer, &updu.cmd, block_index);
		QUEUE_REMOVE_UNITS(consumer->queue,
				   block_buffer + block_index,
				   count - sizeof(updu));
		block_index += count - sizeof(updu);
		block_size -= block_index;
		rx_state_ = rx_inside_block;
		return;
	}

	/* Must be inside block. */
	QUEUE_REMOVE_UNITS(consumer->queue, block_buffer + block_index, count);
	block_index += count;
	block_size -= count;

	if (block_size)
		return;	/* More to come. */

	/*
	 * Ok, the enter block has been received and reassembled, pass it to
	 * the updater for verification and programming.
	 */
	fw_upgrade_command_handler(block_buffer, block_index, &resp_size);

	shared_mem_release(block_buffer);
	resp_value = block_buffer[0];
	QUEUE_ADD_UNITS(&upgrade_to_usb, &resp_value, sizeof(resp_value));
	rx_state_ = rx_outside_block;
}

static void upgrade_flush(struct consumer const *consumer)
{
}

struct consumer const upgrade_consumer = {
	.queue = &usb_to_upgrade,
	.ops   = &((struct consumer_ops const) {
		.written = upgrade_out_handler,
		.flush   = upgrade_flush,
	}),
};
