/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

static void *setup_battery_static(void)
{
	memcpy(&battery_static[0],
	       &(const struct battery_static_info){
		       .model_ext = "NOT-SPICY",
		       .serial_ext = "1234ABCD",
		       .manufacturer_ext = "Honest Eve's Very Safe Batteries",
	       },
	       sizeof(battery_static[0]));
	return NULL;
}

static void before_test(void *state)
{
	battery_memmap_set_index(0);
}

ZTEST_SUITE(acpi_battery, drivers_predicate_post_main, setup_battery_static,
	    before_test, /* after */ NULL, /* teardown */ NULL);

static void set_id(uint8_t id)
{
	acpi_write(EC_ACPI_MEM_STRINGS_FIFO, id);
}

static void read_string(char *s, size_t max_len)
{
	for (size_t i = 0; i < max_len; i++) {
		s[i] = acpi_read(EC_ACPI_MEM_STRINGS_FIFO);
	}
}

static void assert_reads_string(uint8_t id, const char *expected)
{
	const size_t sz = strlen(expected) + 1;
	char s[sz];
	memset(s, 0, sz);

	set_id(id);
	read_string(s, sz);
	zassert_mem_equal(s, expected, sz, "expected \"%s\", but read \"%s\"",
			  expected, s);
}

ZTEST_USER(acpi_battery, test_fifo_version)
{
	/* The first byte of data says we implement version 1. */
	acpi_write(EC_ACPI_MEM_STRINGS_FIFO,
		   EC_ACPI_MEM_STRINGS_FIFO_ID_VERSION);
	zassert_equal(acpi_read(EC_ACPI_MEM_STRINGS_FIFO),
		      EC_ACPI_MEM_STRINGS_FIFO_V1);

	/* Subsequent bytes are zero (no more data to return). */
	zassert_equal(acpi_read(EC_ACPI_MEM_STRINGS_FIFO), 0);
}

ZTEST_USER(acpi_battery, test_read_model)
{
	assert_reads_string(EC_ACPI_MEM_STRINGS_FIFO_ID_BATTERY_MODEL,
			    "NOT-SPICY");
}

ZTEST_USER(acpi_battery, test_read_serial)
{
	assert_reads_string(EC_ACPI_MEM_STRINGS_FIFO_ID_BATTERY_SERIAL,
			    "1234ABCD");
}

ZTEST_USER(acpi_battery, test_read_manufacturer)
{
	/*
	 * This string is exactly the same length as the field containing it,
	 * exercising the path that checks for buffer overrun (causing the
	 * string to end at that point).
	 */
	const char expected[] = "Honest Eve's Very Safe Batteries";
	_Static_assert(
		sizeof(expected) ==
			(sizeof(battery_static[0].manufacturer_ext) + 1),
		"Expected string must completely fill battery buffer before the terminator");

	assert_reads_string(EC_ACPI_MEM_STRINGS_FIFO_ID_BATTERY_MANUFACTURER,
			    expected);
}

ZTEST_USER(acpi_battery, test_unknown_id)
{
	/* An unrecognized string ID always reads 0 (empty string) */
	assert_reads_string(0x5a, "");
}

ZTEST_USER(acpi_battery, test_invalid_battery_index)
{
	/* An invalid battery index always reads empty strings */
	battery_memmap_set_index(BATT_IDX_INVALID);
	assert_reads_string(EC_ACPI_MEM_STRINGS_FIFO_ID_BATTERY_SERIAL, "");
}
