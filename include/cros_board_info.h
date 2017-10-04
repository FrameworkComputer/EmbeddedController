/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info
 */
#ifndef __CROS_EC_CROS_BOARD_INFO_H
#define __CROS_EC_CROS_BOARD_INFO_H

#include "common.h"

#define CBI_VERSION_MAJOR 0
#define CBI_VERSION_MINOR 0
static const uint8_t cbi_magic[] = { 0x43, 0x42, 0x49 };  /* 'C' 'B' 'I' */

struct cbi_header {
	uint8_t magic[3];
	/* CRC of 'struct board_info' excluding magic and crc */
	uint8_t crc;
	/* Data format version. Parsers are expected to process data as long
	 * as major version is equal or younger. */
	union {
		struct {
			uint8_t minor_version;
			uint8_t major_version;
		};
		uint16_t version;
	};
	/* Total size of data. It can be larger than sizeof(struct board_info)
	 * if future versions add additional fields. */
	uint16_t total_size;
} __attribute__((packed));

struct board_info {
	struct cbi_header head;
	/* Board version */
	union {
		struct {
			uint8_t minor_version;
			uint8_t major_version;
		};
		uint16_t version;
	};
	/* OEM ID */
	uint8_t oem_id;
	/* SKU ID */
	uint8_t sku_id;
} __attribute__((packed));

/**
 * Board info accessors
 *
 * @param version/sku_id/oem_id [OUT] Data read from EEPROM
 * @return EC_SUCCESS on success or EC_ERROR_* otherwise.
 */
int cbi_get_board_version(uint32_t *version);
int cbi_get_sku_id(uint32_t *sku_id);
int cbi_get_oem_id(uint32_t *oem_id);

#endif /* __CROS_EC_CROS_BOARD_INFO_H */
