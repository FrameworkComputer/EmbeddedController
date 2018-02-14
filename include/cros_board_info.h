/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info
 */
#ifndef __CROS_EC_CROS_BOARD_INFO_H
#define __CROS_EC_CROS_BOARD_INFO_H

#include "common.h"
#include "ec_commands.h"

#define CBI_VERSION_MAJOR	0
#define CBI_VERSION_MINOR	0
#define CBI_EEPROM_SIZE		256
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
	/* List of data items (i.e. struct cbi_data[]) */
	uint8_t data[];
} __attribute__((packed));

struct cbi_data {
	uint8_t tag;		/* enum cbi_data_tag */
	uint8_t size;		/* size of value[] */
	uint8_t value[];	/* data value */
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

/**
 * Primitive accessors
 *
 * @param tag   Tag of the target data.
 * @param buf   Buffer where data is passed.
 * @param size  (IN) Size of <buf>. (OUT) Size of the data returned.
 * @return EC_SUCCESS on success or EC_ERROR_* otherwise.
 */
int cbi_get_board_info(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size);
int cbi_set_board_info(enum cbi_data_tag tag, const uint8_t *buf, uint8_t size);

#endif /* __CROS_EC_CROS_BOARD_INFO_H */
