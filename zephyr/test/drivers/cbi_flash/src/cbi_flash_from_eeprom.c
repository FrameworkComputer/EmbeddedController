/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_flash.h"
#include "cros_board_info.h"
#include "emul/emul_flash.h"
#include "flash.h"
#include "hooks.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, eeprom_load, int, char *, int);
FAKE_VALUE_FUNC(int, flash_load, int, char *, int);

static int cbi_flash_erase(void)
{
	return crec_flash_physical_erase(CBI_FLASH_OFFSET, CBI_FLASH_SIZE);
}

static int mock_flash_read(int offset, uint8_t *data, int len)
{
	return crec_flash_unprotected_read(CBI_FLASH_OFFSET + offset, len,
					   (char *)data);
}

static int mock_eeprom_read_blank(int offset, uint8_t *data, int len)
{
	memset(data, 0x00, len);
	return EC_SUCCESS;
}

static int mock_eeprom_read_cbi(int offset, uint8_t *data, int len)
{
	struct cbi_header *h = (struct cbi_header *)data;

	memset(h, 0, sizeof(*h));
	h->magic[0] = 'C';
	h->magic[1] = 'B';
	h->magic[2] = 'I';
	h->major_version = CBI_VERSION_MAJOR;
	h->minor_version = CBI_VERSION_MINOR;
	h->total_size = sizeof(*h);
	h->crc = cbi_crc8(h);

	return EC_SUCCESS;
}

static int mock_eeprom_read_error(int offset, uint8_t *data, int len)
{
	return EC_ERROR_UNKNOWN;
}

static int mock_flash_read_error(int offset, uint8_t *data, int len)
{
	return EC_ERROR_UNKNOWN;
}

static int mock_flash_read_bad_version(int offset, uint8_t *data, int len)
{
	struct cbi_header *h = (struct cbi_header *)data;

	memset(h, 0, sizeof(*h));
	h->magic[0] = 'C';
	h->magic[1] = 'B';
	h->magic[2] = 'I';
	h->major_version = 98;
	h->minor_version = 76;
	h->total_size = sizeof(*h);
	h->crc = cbi_crc8(h);

	return EC_SUCCESS;
}

static int mock_flash_read_bad_size(int offset, uint8_t *data, int len)
{
	struct cbi_header *h = (struct cbi_header *)data;

	memset(h, 0, sizeof(*h));
	h->magic[0] = 'C';
	h->magic[1] = 'B';
	h->magic[2] = 'I';
	h->major_version = CBI_VERSION_MAJOR;
	h->minor_version = CBI_VERSION_MINOR;
	h->total_size = 1;

	return EC_SUCCESS;
}

static int mock_flash_read_bad_crc(int offset, uint8_t *data, int len)
{
	struct cbi_header *h = (struct cbi_header *)data;

	memset(h, 0, sizeof(*h));
	h->magic[0] = 'C';
	h->magic[1] = 'B';
	h->magic[2] = 'I';
	h->major_version = CBI_VERSION_MAJOR;
	h->minor_version = CBI_VERSION_MINOR;
	h->total_size = sizeof(*h);

	return EC_SUCCESS;
}

ZTEST(cbi_flash_from_eeprom, test_hook_called)
{
	int prev_eeprom_reads;
	int prev_flash_reads;

	eeprom_load_fake.custom_fake = mock_eeprom_read_blank;
	flash_load_fake.custom_fake = mock_flash_read;

	/*
	 * Verify CBI flash remains invalid if CBI EEPROM is invalid
	 * (missing).
	 */

	for (int i = 0; i < 2; ++i) {
		prev_eeprom_reads = eeprom_load_fake.call_count;
		prev_flash_reads = flash_load_fake.call_count;

		hook_notify(HOOK_INIT);

		zassert_true(flash_load_fake.call_count > prev_flash_reads,
			     "CBI flash not read during HOOK_INIT");
		zassert_true(eeprom_load_fake.call_count > prev_eeprom_reads,
			     "CBI EERPM not read during HOOK_INIT");
	}
}

ZTEST(cbi_flash_from_eeprom, test_cbi_copy)
{
	int prev_eeprom_reads;
	int prev_flash_reads;

	eeprom_load_fake.custom_fake = mock_eeprom_read_cbi;
	flash_load_fake.custom_fake = mock_flash_read;

	/*
	 * trigger cros_cbi_transfer_eeprom_to_flash() call by
	 * triggering HOOK_INIT which is how it is normally invoked.
	 *
	 * Without CBI populated in flash, CBI EEPROM is expected to be
	 * checked and (if valid) copied to flash.
	 */

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > 0,
		     "CBI flash not read during HOOK_INIT");
	zassert_true(eeprom_load_fake.call_count > 0,
		     "CBI EEPROM not read during HOOK_INIT");

	/*
	 * CBI EEPROM should now have been copied to flash and no longer
	 * be accessed.
	 */

	prev_eeprom_reads = eeprom_load_fake.call_count;
	prev_flash_reads = flash_load_fake.call_count;

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > prev_flash_reads,
		     "CBI flash not read during 2nd HOOK_INIT");
	zassert_equal(eeprom_load_fake.call_count, prev_eeprom_reads,
		      "CBI EEPROM read during 2nd HOOK_INIT");
}

ZTEST(cbi_flash_from_eeprom, test_bad_flash)
{
	eeprom_load_fake.custom_fake = mock_eeprom_read_cbi;
	flash_load_fake.custom_fake = mock_flash_read_error;

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > 0,
		     "CBI flash not read during HOOK_INIT");
	zassert_true(eeprom_load_fake.call_count == 0,
		     "CBI EEPROM not read during HOOK_INIT");
}

ZTEST(cbi_flash_from_eeprom, test_bad_eeprom)
{
	eeprom_load_fake.custom_fake = mock_eeprom_read_error;
	flash_load_fake.custom_fake = mock_flash_read;

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > 0,
		     "CBI flash not read during HOOK_INIT");
	zassert_true(eeprom_load_fake.call_count > 0,
		     "CBI EEPROM not read during HOOK_INIT");
}

ZTEST(cbi_flash_from_eeprom, test_bad_version)
{
	eeprom_load_fake.custom_fake = mock_eeprom_read_cbi;
	flash_load_fake.custom_fake = mock_flash_read_bad_version;

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > 0,
		     "CBI flash not read during HOOK_INIT");
	zassert_true(eeprom_load_fake.call_count > 0,
		     "CBI EEPROM not read during HOOK_INIT");
}

ZTEST(cbi_flash_from_eeprom, test_bad_size)
{
	eeprom_load_fake.custom_fake = mock_eeprom_read_cbi;
	flash_load_fake.custom_fake = mock_flash_read_bad_size;

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > 0,
		     "CBI flash not read during HOOK_INIT");
	zassert_true(eeprom_load_fake.call_count > 0,
		     "CBI EEPROM not read during HOOK_INIT");
}

ZTEST(cbi_flash_from_eeprom, test_bad_crc)
{
	eeprom_load_fake.custom_fake = mock_eeprom_read_cbi;
	flash_load_fake.custom_fake = mock_flash_read_bad_crc;

	hook_notify(HOOK_INIT);

	zassert_true(flash_load_fake.call_count > 0,
		     "CBI flash not read during HOOK_INIT");
	zassert_true(eeprom_load_fake.call_count > 0,
		     "CBI EEPROM not read during HOOK_INIT");
}

static void cbi_flash_from_eeprom_before(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(eeprom_load);
	RESET_FAKE(flash_load);

	zassert_ok(cbi_flash_erase());
}

ZTEST_SUITE(cbi_flash_from_eeprom, drivers_predicate_post_main, NULL,
	    cbi_flash_from_eeprom_before, NULL, NULL);
