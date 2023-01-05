/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * flash storage function for seldom updated flash flags
 */

#ifndef __CROS_EC_FLASHSTORAGE_H
#define __CROS_EC_FLASHSTORAGE_H

#define SPI_FLAGS_REGION (0x80000)

enum ec_flash_flags_idx {
	FLASH_FLAGS_ACPOWERON = 0,
	FLASH_FLAGS_STANDALONE = 1,
	FLASH_FLAGS_MAX = 64
};

#define FLASH_FLAGS_MAGIC (0xF1A3)
#define FLASH_FLAGS_VERSION (0x1)

struct ec_flash_flags_info {
	/* Header */
	uint32_t magic; /* 0xF1A3 */
	uint32_t length; /* Length of fields following this */
	uint32_t version; /* Version=1, update this if field structures below this change */
	/**
	 * An incrementing counter that should be incremented
	 * every time the structure is written to flash
	 */
	uint32_t update_number;

	uint8_t flags[FLASH_FLAGS_MAX];

} __ec_align1;

/**
 * @brief Update flags value at idx, but does not write to flash
 *
 * @param idx ec_flash_flags_idx for which flag to update
 * @param v uint8_t new value
 * @return int EC_SUCCESS
 */
int flash_storage_update(enum ec_flash_flags_idx idx, uint8_t v);

/**
 * @brief Commits storage if dirty
 *
 * @return int EC_SUCCESS
 */
int flash_storage_commit(void);

/**
 * @brief Get flags value
 *
 * @param idx index of flag to get
 * @return int flag value, or -1 on failure
 */
int flash_storage_get(enum ec_flash_flags_idx idx);

/**
 * @brief Initialize structure in memory, does not update flash
 *
 * @return int EC_SUCCESS
 */
void flash_storage_load_defaults(void);

#endif	/* __CROS_EC_FLASHSTORAGE_H */
