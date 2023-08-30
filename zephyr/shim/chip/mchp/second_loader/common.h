/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _COMMON_h_
#define _COMMON_h_

#define SECTOR_SIZE 4096

enum failure_resp_type {
	NO_FAILURE = 0,
	PACKET_PAYLOAD_ILLEGAL_LEN,
	PACKET_CRC_FAILURE,
	HEADER_PACKET_ILLEGAL_OFFSET,
	HEADER_PACKET_INVALID,
	PGM_PACKET_ILLEGAL_OFFSET,
	PGM_FLASH_DATA_LEN_INCORRECT,
	SPI_OPERATION_FAILURE,
	/* Internal errors without a corresponding host response type */
	INTERNAL_ERROR_START, /* Placeholder */
	BOARD_INIT_ERR,
	SERIAL_RECV_TIMEOUT
};

#endif
