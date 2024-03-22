/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_config.h"
#include "cbi_transfer.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cbi_transfer, LOG_LEVEL_ERR);

BUILD_ASSERT(
	CBI_IMAGE_SIZE == CBI_IMAGE_SIZE_EEPROM,
	"CBI image size in EC flash is not equal to CBI image size in EEPROM");

static bool is_valid_cbi(const uint8_t *cbi)
{
	const struct cbi_header *head = (const struct cbi_header *)cbi;

	/* Check magic */
	if (memcmp(head->magic, cbi_magic, sizeof(head->magic))) {
		return false;
	}

	/* check version */
	if (head->major_version > CBI_VERSION_MAJOR) {
		return false;
	}

	/*
	 * Check the data size. It's expected to support up to 64k but our
	 * buffer has practical limitation.
	 */
	if (head->total_size < sizeof(*head) ||
	    head->total_size > CBI_IMAGE_SIZE) {
		return false;
	}

	/* Check CRC */
	if (cbi_crc8(head) != head->crc) {
		return false;
	}

	return true;
}

void cros_cbi_transfer_eeprom_to_flash(void)
{
	uint8_t cbi[CBI_IMAGE_SIZE];
	int rv;

	rv = flash_cbi_config.drv->load(0, cbi, CBI_IMAGE_SIZE);
	if (rv) {
		LOG_ERR("Failed to read CBI from EC Flash");
		return;
	}
	if (!is_valid_cbi(cbi)) {
		rv = eeprom_cbi_config.drv->load(0, cbi, CBI_IMAGE_SIZE);
		if (rv) {
			LOG_ERR("Failed to read CBI from EEPROM");
			return;
		}
		if (is_valid_cbi(cbi)) {
			rv = flash_cbi_config.drv->store(cbi);
			if (rv) {
				LOG_ERR("Failed to write CBI in EC Flash");
				return;
			}
		} else {
			LOG_ERR("Bad EEPROM data, transfer to EC flash failed");
			return;
		}
	}
}
