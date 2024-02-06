/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "sysjump.h"
#include "test/drivers/test_state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <sys/types.h>

/**
 * @brief Returns a pointer to an object (such as a struct jump_data) of type
 *        TYPE at the end of the mock_jump_data memory region, plus an optional,
 *        additional offset of OFFSET bytes. OFFSET can be used to help get the
 *        pointer after jump data has been moved by get_panic_data_write(), or
 *        left as zero to get the pre-move location.
 */
#define GET_JUMP_DATA_PTR(TYPE, OFFSET)                                    \
	((TYPE *)(mock_jump_data + sizeof(mock_jump_data) - sizeof(TYPE) + \
		  (OFFSET)))

ZTEST(panic_output_get_panic_data_write, test_existing_panic_data)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();

	/* Pretend panic data exists by setting the magic header and setting its
	 * size.
	 */
	pdata_actual->magic = PANIC_DATA_MAGIC;
	pdata_actual->struct_size = CONFIG_PANIC_DATA_SIZE;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());
}

ZTEST(panic_output_get_panic_data_write, test_no_panic_data__no_jump_data)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct panic_data pdata_expected = {
		.magic = PANIC_DATA_MAGIC,
		.struct_size = CONFIG_PANIC_DATA_SIZE,
	};

	/* Don't fill in any panic data, but add some fake data so we can ensure
	 * it gets reset to zero.
	 */
	pdata_actual->flags = 0xFF;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify the pdata struct has correct fields filled out. */
	zassert_mem_equal(&pdata_expected, pdata_actual,
			  sizeof(struct panic_data));
}

/**
 * @brief Implements the fields of a version 1 jump_data header.
 *
 */
struct jump_data_v1 {
	/** V1 Jump data header, always goes at end. See sysjump.h for info */

	uint32_t reset_flags;
	int version;
	int magic;
};

/* Test that V1 jump data is moved correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_v1)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v1 jdata_expected = {
		.magic = JUMP_DATA_MAGIC,
		.version = 1,
		.reset_flags = 0xAABBCCDD,
	};

	/* Set up some jump data. Version 1 does not have any jump tags, only
	 * the magic, version number, and reset_flags so it is constant size.
	 */
	*GET_JUMP_DATA_PTR(struct jump_data_v1, 0) = jdata_expected;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data has been moved to its new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data_v1 *jdata_moved =
		GET_JUMP_DATA_PTR(struct jump_data_v1, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved,
			  sizeof(struct jump_data_v1));
}

/**
 * @brief Implements a V2 jump_data header plus some extra bytes in front that
 *        represent jump tag data. This reflects how they are stored in the EC's
 *        memory. The jump_tag_total field in jdata stored how many bytes of
 *        preceding jump tag data exists.
 */
struct jump_data_v2_plus_tags {
	/** Arbitrary amount of jump tag data. */
	uint8_t tag_data[8];
	/** V2 Jump data header, always goes at end. See sysjump.h for info */
	struct {
		int jump_tag_total;
		uint32_t reset_flags;
		int version;
		int magic;
	} jdata;
};

/* Test that V2 jump data is moved correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_v2)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v2_plus_tags jdata_expected = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = JUMP_DATA_MAGIC,
			.version = 2,
			.reset_flags = 0xAABBCCDD,
			.jump_tag_total = sizeof(jdata_expected.tag_data),
		},
	};

	/* Set up some jump data and preceding tags */
	*GET_JUMP_DATA_PTR(struct jump_data_v2_plus_tags, 0) = jdata_expected;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data and tags moved to their new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data_v2_plus_tags *jdata_moved = GET_JUMP_DATA_PTR(
		struct jump_data_v2_plus_tags, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved,
			  sizeof(struct jump_data_v2_plus_tags));
}

/**
 * @brief Implements a V3 jump_data header with space in front for jump tag data
 */
struct jump_data_v3_plus_tags {
	/** Arbitrary amount of jump tag data. */
	uint8_t tag_data[8];
	/** V3 Jump data header, always goes at end. See sysjump.h for info */
	struct {
		uint8_t reserved0;
		int struct_size;
		int jump_tag_total;
		uint32_t reset_flags;
		int version;
		int magic;
	} jdata;
};

/* Test that V3 jump data is moved correctly. */
ZTEST(panic_output_get_panic_data_write, test_no_panic_data__jump_data_v3)
{
	struct panic_data *pdata_actual = test_get_panic_data_pointer();
	struct jump_data_v3_plus_tags jdata_expected = {
		/* Arbitrary jump tag data */
		.tag_data = {1, 2, 3, 4, 5, 6, 7, 8},
		.jdata = {
			.magic = JUMP_DATA_MAGIC,
			.version = 3,
			.reset_flags = 0xAABBCCDD,
			.jump_tag_total = sizeof(jdata_expected.tag_data),
			.struct_size = sizeof(jdata_expected.jdata),
			.reserved0 = 0xFF,
		},
	};

	/* Set up some jump data and preceding tags */
	*GET_JUMP_DATA_PTR(struct jump_data_v3_plus_tags, 0) = jdata_expected;

	/* Verify that pdata_ptr is returned */
	zassert_equal(pdata_actual, get_panic_data_write());

	/* Verify that jump data and tags moved to their new location */
	ssize_t expected_move_delta = -1 * sizeof(struct panic_data);
	struct jump_data_v3_plus_tags *jdata_moved = GET_JUMP_DATA_PTR(
		struct jump_data_v3_plus_tags, expected_move_delta);

	zassert_mem_equal(&jdata_expected, jdata_moved,
			  sizeof(struct jump_data_v3_plus_tags));
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	struct panic_data *pdata = test_get_panic_data_pointer();

	memset(pdata, 0, sizeof(struct panic_data));
	memset(mock_jump_data, 0, sizeof(mock_jump_data));
}

ZTEST_SUITE(panic_output_get_panic_data_write, drivers_predicate_post_main,
	    NULL, reset, reset, NULL);
