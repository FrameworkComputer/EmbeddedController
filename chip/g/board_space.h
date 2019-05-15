/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_BOARD_SPACE_H
#define __EC_CHIP_G_BOARD_SPACE_H

#include "compile_time_macros.h"
#include "flash_config.h"
#include "flash_info.h"
#include "stdint.h"

/*
 * Structures for data stored in the board space of INFO1.
 */

/* Structure holding Board ID */
struct board_id {
	uint32_t type;		/* Board type */
	uint32_t type_inv;	/* Board type (inverted) */
	uint32_t flags;		/* Flags */
};

/* Structure holding serial number data */
struct sn_data {
	uint8_t version;
	uint8_t reserved[2];
	uint8_t rma_status;
	uint32_t sn_hash[3];
};

/* Current sn_data format version */
#define SN_DATA_VERSION	0x0f
/* Size of header elements (everything apart from sn_hash) */
#define SN_HEADER_SIZE	offsetof(struct sn_data, sn_hash)
/* Number of bits reserved for RMA counter */
#define RMA_COUNT_BITS	7
/* Value used to indicate device has been RMA'd */
#define RMA_INDICATOR	((uint8_t) ~BIT(RMA_COUNT_BITS))

/* Info1 Board space contents. */
struct info1_board_space {
	struct board_id bid;
	/* Pad so that board_id occupies it's full 'protect' size */
	uint8_t bid_padding[4];
	struct sn_data sn;
};

/*
 * Layout of the entire 2K INFO1 space.
 *
 * - ro_info_map - maps controlling ro and rw images rollback protection.
 * - rw_info_map
 * - board_space - various objects used by Chrome OS applications
 * - manufacture_space - seed used for generating and verification of
 *                        endorsement certs.
 */
struct info1_layout {
	uint8_t ro_info_map[INFO_RO_MAP_SIZE];
	uint8_t rw_info_map[INFO_RW_MAP_SIZE];
	struct info1_board_space board_space;
	uint8_t padding[FLASH_INFO_MANUFACTURE_STATE_OFFSET - INFO_RO_MAP_SIZE -
			INFO_RW_MAP_SIZE - sizeof(struct info1_board_space)];
	uint8_t manufacture_space[FLASH_INFO_MANUFACTURE_STATE_SIZE];
};
BUILD_ASSERT(sizeof(struct info1_layout) == FLASH_INFO_SIZE);

#define INFO_BOARD_ID_SIZE		sizeof(struct board_id)
#define INFO_BOARD_ID_OFFSET		(INFO_BOARD_SPACE_OFFSET + \
					 offsetof(struct info1_board_space, \
						  bid))

#define INFO_SN_DATA_SIZE		 sizeof(struct sn_data)
#define INFO_SN_DATA_OFFSET		 (INFO_BOARD_SPACE_OFFSET + \
					  offsetof(struct info1_board_space, \
						   sn))

/*
 * Write protection for the INFO1 space allows windows with sizes that are
 * powers of 2 to be protected. Given the different write restrictions on
 * the different spaces listed above, we keep them in separate windows.
 * This implies that each space must occupy a space that has a size which
 * is a power of two.
 */
#define INFO_BOARD_ID_PROTECT_SIZE	16
#define INFO_SN_DATA_PROTECT_SIZE	16

BUILD_ASSERT((INFO_BOARD_ID_SIZE & 3) == 0);
BUILD_ASSERT((INFO_BOARD_ID_OFFSET & 3) == 0);
BUILD_ASSERT(INFO_BOARD_ID_SIZE <= INFO_BOARD_ID_PROTECT_SIZE);

BUILD_ASSERT((INFO_SN_DATA_SIZE & 3) == 0);
BUILD_ASSERT((INFO_SN_DATA_OFFSET & 3) == 0);
BUILD_ASSERT(INFO_SN_DATA_SIZE <= INFO_SN_DATA_PROTECT_SIZE);

#endif  /* ! __EC_CHIP_G_BOARD_SPACE_H */
