/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_UPGRADE_FW_H
#define __EC_CHIP_G_UPGRADE_FW_H

#include <stddef.h>


/*
 * This file contains structures used to facilitate cr50 firmware updates,
 * which can be used on any g chip.
 *
 * The firmware update protocol consists of two phases: connection
 * establishment and actual image transfer.
 *
 * Image transfer is done in 1K blocks. The host supplying the image
 * encapsulates blocks in frames by prepending a header including the flash
 * offset where the block is destined and its digest.
 *
 * The CR50 device responds to each frame with a confirmation which is 1 byte
 * response. Zero value means success, non zero value is the error code
 * reported by CR50.
 *
 * To establish the connection, the host sends a different frame, which
 * contains no data and is destined to offset 0. Receiving such a frame
 * signals the CR50 that the host intends to transfer a new image.
 *
 * Version 3 connection establishment response is 16 bytes in size, all values
 * in network byte order. The first 4 bytes are the error code (if any), the
 * second 4 bytes are the protocol version (set to 3) and then 4 byte offset
 * of the RO section followed by the 4 byte offset of the RW section.
 */

#define UPGRADE_PROTOCOL_VERSION 3

/* This is the format of the update frame header. */
struct upgrade_command {
	uint32_t  block_digest;  /* first 4 bytes of sha1 of the rest of the
				  * frame.
				  */
	uint32_t  block_base;    /* Offset of this frame into the flash SPI. */
	/* The actual payload goes here. */
} __packed;

/*
 * This is the frame format the host uses when sending update PDUs over USB.
 *
 * The PDUs are up to 1K bytes in size, they are fragmented into USB chunks of
 * 64 bytes each and reassembled on the receive side before being passed to
 * the flash update function.
 *
 * The flash update function receives the unframed PDU body (starting at the
 * cmd field below), and puts its reply into the same buffer the PDU was in.
 */
struct update_frame_header {
	uint32_t block_size;    /* Total size of the block, including this
				 * field.
				 */
	struct upgrade_command cmd;
};

/*
 * Response to the connection establishment request.
 *
 * When responding to the very first packet of the upgrade sequence, the
 * original USB update implementation was responding with a four byte value,
 * just as to any other block of the transfer sequence.
 *
 * It became clear that there is a need to be able to enhance the upgrade
 * protocol, while stayng backwards compatible.
 *
 * All newer protocol versions (satring with version 2) respond to the very
 * first packet with an 8 byte or larger response, where the first 4 bytes are
 * a version specific data, and the second 4 bytes - the protocol version
 * number.
 *
 * This way the host receiving of a four byte value in response to the first
 * packet is considered an indication of the target running the 'legacy'
 * protocol, version 1. Receiving of an 8 byte or longer response would
 * communicates the protocol version in the second 4 bytes.
 */
struct first_response_pdu {
	uint32_t return_value;
	uint32_t protocol_version;
	union {
		struct {
			uint32_t  backup_ro_offset;
			uint32_t  backup_rw_offset;
		} vers3;
	};
};

/* TODO: Handle this in upgrade_fw.c, not usb_upgrade.c */
#define UPGRADE_DONE          0xB007AB1E

void fw_upgrade_command_handler(void *body,
				size_t cmd_size,
				size_t *response_size);


/* Various upgrade command return values. */
enum return_value {
	UPGRADE_SUCCESS = 0,
	UPGRADE_BAD_ADDR = 1,
	UPGRADE_ERASE_FAILURE = 2,
	UPGRADE_DATA_ERROR = 3,
	UPGRADE_WRITE_FAILURE = 4,
	UPGRADE_VERIFY_ERROR = 5,
	UPGRADE_GEN_ERROR = 6,
	UPGRADE_MALLOC_ERROR = 7,
};

#endif  /* ! __EC_CHIP_G_UPGRADE_FW_H */
