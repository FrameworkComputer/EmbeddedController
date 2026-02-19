/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "panic.h"
#include "sysjump.h"
#include "system.h"

#include <string.h>

#include <zephyr/ztest.h>

extern char mock_end_of_ram_data[CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE];

struct jump_data *get_jump_data(void);

/**
 * @brief Returns a pointer to an object (such as a struct jump_data) of type
 *        TYPE at the end of the mock_end_of_ram_data memory region, plus an
 * optional, additional offset of OFFSET bytes. OFFSET can be used to help get
 * the pointer after jump data has been moved by get_panic_data_write(), or left
 * as zero to get the pre-move location.
 */
#define GET_JUMP_DATA_PTR(TYPE, OFFSET)                                 \
	((TYPE *)(mock_end_of_ram_data + sizeof(mock_end_of_ram_data) - \
		  sizeof(TYPE) + (OFFSET)))

#define ROUNDUP4(x) (((x) + 3) & ~3)

/*
 * Redefine jump_tag since it's static in common/system.c
 */
struct jump_tag {
	uint16_t tag; /* Tag ID */
	uint8_t data_size; /* Size of data which follows */
	uint8_t data_version; /* Data version */
};

static void jump_data_before(void *data)
{
	memset(mock_end_of_ram_data, 0, sizeof(mock_end_of_ram_data));
	system_common_reset_state();
}

ZTEST(jump_data, test_init_no_jump_data)
{
	struct jump_data *jdata = get_jump_data();

	/* Set up invalid magic */
	jdata->magic = 0xdeadbeef;
	jdata->version = JUMP_DATA_VERSION;

	system_common_pre_init();

	/* Should be cleared */
	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->version, 0);
	zassert_equal(system_jumped_to_this_image(), 0);
}

/**
 * @brief Implements the fields of a version 1 jump_data header.
 *
 */
struct jump_data_v1 {
	uint32_t reset_flags;
	int version;
	int magic;
};

ZTEST(jump_data, test_init_v1_jump_data)
{
	struct jump_data_v1 *v1_ptr = GET_JUMP_DATA_PTR(struct jump_data_v1, 0);
	v1_ptr->reset_flags = EC_RESET_FLAG_POWER_ON;
	v1_ptr->version = 1;
	v1_ptr->magic = JUMP_DATA_MAGIC;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);

	struct jump_data *jdata = get_jump_data();
	zassert_equal(jdata->version, 1);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->jump_tag_total, 0);
}

/**
 * @brief Implements the fields of a version 2 jump_data header.
 *
 */
struct jump_data_v2 {
	int jump_tag_total;
	uint32_t reset_flags;
	int version;
	int magic;
};

ZTEST(jump_data, test_init_v2_jump_data)
{
	struct jump_data_v2 *v2_ptr = GET_JUMP_DATA_PTR(struct jump_data_v2, 0);
	v2_ptr->jump_tag_total = 8;
	v2_ptr->reset_flags = EC_RESET_FLAG_POWER_ON;
	v2_ptr->version = 2;
	v2_ptr->magic = JUMP_DATA_MAGIC;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);

	struct jump_data *jdata = get_jump_data();

	zassert_equal(jdata->version, 2);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
	zassert_equal(jdata->jump_tag_total, 8);
}

ZTEST(jump_data, test_init_v2_with_tags)
{
	uint32_t tag_data_expected = 0xcafebeef;
	int total_tag_size =
		sizeof(struct jump_tag) + sizeof(tag_data_expected);
	struct jump_data_v2 *v2_ptr = GET_JUMP_DATA_PTR(struct jump_data_v2, 0);
	v2_ptr->jump_tag_total = total_tag_size;
	v2_ptr->reset_flags = EC_RESET_FLAG_SOFT;
	v2_ptr->version = 2;
	v2_ptr->magic = JUMP_DATA_MAGIC;

	struct jump_tag *tag =
		(struct jump_tag *)((uint8_t *)v2_ptr - total_tag_size);
	tag->tag = 0x1234;
	tag->data_version = 1;
	tag->data_size = sizeof(tag_data_expected);
	memcpy((uint8_t *)tag + sizeof(struct jump_tag), &tag_data_expected,
	       sizeof(tag_data_expected));

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_SOFT | EC_RESET_FLAG_SYSJUMP);

	struct jump_data *jdata = get_jump_data();
	zassert_equal(jdata->jump_tag_total, total_tag_size);
	zassert_equal(jdata->version, 2);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
	zassert_equal(jdata->jump_tag_total, total_tag_size);

	// Check tag
	int tag_version;
	int tag_size;
	uint32_t tag_data;

	tag_data = *(uint32_t *)system_get_jump_tag(0x1234, &tag_version,
						    &tag_size);
	zassert_equal(tag_data, tag_data_expected);
	zassert_equal(tag_version, 1);
	zassert_equal(tag_size, sizeof(tag_data_expected));
}

/**
 * @brief Implements the fields of a version 3 jump_data header.
 *
 */
struct jump_data_v3 {
	uint8_t reserved0;
	int struct_size;
	int jump_tag_total;
	uint32_t reset_flags;
	int version;
	int magic;
};

ZTEST(jump_data, test_init_v3_jump_data)
{
	struct jump_data_v3 *v3_ptr = GET_JUMP_DATA_PTR(struct jump_data_v3, 0);
	v3_ptr->reserved0 = 0;
	v3_ptr->struct_size = sizeof(struct jump_data_v3);
	v3_ptr->jump_tag_total = 0;
	v3_ptr->reset_flags = EC_RESET_FLAG_POWER_ON;
	v3_ptr->version = 3;
	v3_ptr->magic = JUMP_DATA_MAGIC;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);

	struct jump_data *jdata = get_jump_data();

	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->version, 3);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
	zassert_equal(jdata->jump_tag_total, 0);
}

ZTEST(jump_data, test_init_v3_with_tags)
{
	uint32_t tag_data_expected = 0xcafebeef;
	int total_tag_size =
		sizeof(struct jump_tag) + sizeof(tag_data_expected);

	struct jump_data_v3 *v3_ptr = GET_JUMP_DATA_PTR(struct jump_data_v3, 0);
	v3_ptr->reserved0 = 0;
	v3_ptr->struct_size = sizeof(struct jump_data_v3);
	v3_ptr->jump_tag_total = total_tag_size;
	v3_ptr->reset_flags = EC_RESET_FLAG_POWER_ON;
	v3_ptr->version = 3;
	v3_ptr->magic = JUMP_DATA_MAGIC;

	struct jump_tag *tag =
		(struct jump_tag *)((uint8_t *)v3_ptr - total_tag_size);
	tag->tag = 0x1234;
	tag->data_version = 1;
	tag->data_size = sizeof(tag_data_expected);
	memcpy((uint8_t *)tag + sizeof(struct jump_tag), &tag_data_expected,
	       sizeof(tag_data_expected));

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);

	struct jump_data *jdata = get_jump_data();
	zassert_equal(jdata->jump_tag_total, total_tag_size);
	zassert_equal(jdata->version, 3);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
	zassert_equal(jdata->jump_tag_total, total_tag_size);

	// Check tag
	int tag_version;
	int tag_size;
	uint32_t tag_data;

	tag_data = *(uint32_t *)system_get_jump_tag(0x1234, &tag_version,
						    &tag_size);
	zassert_equal(tag_data, tag_data_expected);
	zassert_equal(tag_version, 1);
	zassert_equal(tag_size, sizeof(tag_data_expected));
}

/**
 * @brief Implements a hypothetical version 99 jump_data header.
 *
 */
struct jump_data_v99 {
	uint32_t new_v99_field;
	uint8_t reserved0;
	int struct_size;
	int jump_tag_total;
	uint32_t reset_flags;
	int version;
	int magic;
};

ZTEST(jump_data, test_init_v99_jump_data)
{
	struct jump_data_v99 *v99_ptr =
		GET_JUMP_DATA_PTR(struct jump_data_v99, 0);

	v99_ptr->new_v99_field = 0xaabbccdd;
	v99_ptr->reserved0 = 0;
	v99_ptr->struct_size = sizeof(struct jump_data_v99);
	v99_ptr->jump_tag_total = 0;
	v99_ptr->reset_flags = EC_RESET_FLAG_HARD;
	v99_ptr->version = 99;
	v99_ptr->magic = JUMP_DATA_MAGIC;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	struct jump_data *jdata = get_jump_data();

	zassert_equal(jdata->version, 99);
	zassert_equal(jdata->jump_tag_total, 0);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
	zassert_equal(jdata->magic, 0);
}

ZTEST(jump_data, test_init_v99_jump_data_with_tags)
{
	struct jump_data_v99 *v99_ptr =
		GET_JUMP_DATA_PTR(struct jump_data_v99, 0);

	uint32_t tag_data_expected = 0xcafebeef;
	int total_tag_size =
		sizeof(struct jump_tag) + sizeof(tag_data_expected);

	v99_ptr->new_v99_field = 0xaabbccdd;
	v99_ptr->reserved0 = 0;
	v99_ptr->struct_size = sizeof(struct jump_data_v99);
	v99_ptr->jump_tag_total = total_tag_size;
	v99_ptr->reset_flags = EC_RESET_FLAG_HARD;
	v99_ptr->version = 99;
	v99_ptr->magic = JUMP_DATA_MAGIC;

	struct jump_tag *tag =
		(struct jump_tag *)((uint8_t *)v99_ptr - total_tag_size);
	tag->tag = 0x1234;
	tag->data_version = 1;
	tag->data_size = sizeof(tag_data_expected);
	memcpy((uint8_t *)tag + sizeof(struct jump_tag), &tag_data_expected,
	       sizeof(tag_data_expected));

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	struct jump_data *jdata = get_jump_data();

	zassert_equal(jdata->version, 99);
	zassert_equal(jdata->jump_tag_total, total_tag_size);
	zassert_equal(jdata->struct_size, sizeof(struct jump_data));
	zassert_equal(jdata->reserved0, 0);
	zassert_equal(jdata->magic, 0);

	// Check tag
	int tag_version;
	int tag_size;
	uint32_t tag_data;

	tag_data = *(uint32_t *)system_get_jump_tag(0x1234, &tag_version,
						    &tag_size);
	zassert_equal(tag_data, tag_data_expected);
	zassert_equal(tag_version, 1);
	zassert_equal(tag_size, sizeof(tag_data_expected));
}

ZTEST(jump_data, test_init_jump_data_out_of_space)
{
	struct jump_data *jdata = get_jump_data();
	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = sizeof(struct jump_data);

	jdata->jump_tag_total =
		(uintptr_t)jdata - (uintptr_t)mock_end_of_ram_data + 1;

	system_common_pre_init();

	zassert_equal(jdata->magic, 0);
	zassert_equal(jdata->version, 0);
	zassert_equal(system_jumped_to_this_image(), 1);
}

ZTEST(jump_data, test_init_with_panic_data)
{
	struct panic_data *pdata = get_panic_data_write();

	struct jump_data *expected_jdata =
		(struct jump_data *)((uintptr_t)pdata -
				     sizeof(struct jump_data));

	zassert_equal((uintptr_t)get_jump_data(), (uintptr_t)expected_jdata);

	zassert_true((uintptr_t)expected_jdata > JUMP_DATA_MIN_ADDRESS);

	expected_jdata->magic = JUMP_DATA_MAGIC;
	expected_jdata->version = 3;
	expected_jdata->struct_size = sizeof(struct jump_data);
	expected_jdata->jump_tag_total = 0;
	expected_jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	system_common_pre_init();

	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(expected_jdata->magic, 0);
	zassert_equal(system_usable_ram_end(), (uintptr_t)expected_jdata);
}

ZTEST(jump_data, test_init_corrupted_jump_tag_total)
{
	struct jump_data *jdata = GET_JUMP_DATA_PTR(struct jump_data, 0);

	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = sizeof(struct jump_data);
	jdata->jump_tag_total = -100; /* Negative total! */
	jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	/* Should be safely caught and cleared */
	system_common_pre_init();

	zassert_equal(jdata->magic, 0);
	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);
}

ZTEST(jump_data, test_init_corrupted_struct_size)
{
	struct jump_data *jdata = GET_JUMP_DATA_PTR(struct jump_data, 0);

	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->struct_size = -1;
	jdata->jump_tag_total = 8;
	jdata->reset_flags = EC_RESET_FLAG_POWER_ON;

	/* Should be safely caught and cleared */
	system_common_pre_init();

	zassert_equal(jdata->magic, 0);
	zassert_equal(system_jumped_to_this_image(), 1);
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_POWER_ON | EC_RESET_FLAG_SYSJUMP);
}

ZTEST(jump_data, test_init_watchdog_reset)
{
	struct jump_data *jdata = GET_JUMP_DATA_PTR(struct jump_data, 0);

	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = 3;
	jdata->reset_flags = EC_RESET_FLAG_WATCHDOG;
	jdata->struct_size = sizeof(struct jump_data);
	jdata->jump_tag_total = 0;

	system_common_pre_init();

	/* Verify the watchdog flag was preserved and combined with sysjump */
	zassert_equal(system_get_reset_flags(),
		      EC_RESET_FLAG_WATCHDOG | EC_RESET_FLAG_SYSJUMP,
		      "Reset flags: 0x%x", system_get_reset_flags());

	/*
	 * Verify that a watchdog panic was logged if in RW, or NOT logged
	 * if in RO.
	 */
	uint32_t reason, info;
	uint8_t exception;
	panic_get_reason(&reason, &info, &exception);
	if (IS_ENABLED(SECTION_IS_RW)) {
		zassert_equal(reason, PANIC_SW_WATCHDOG_HARD,
			      "Panic reason: %d", reason);
	} else {
		zassert_not_equal(reason, PANIC_SW_WATCHDOG_HARD,
				  "Panic reason should not be set in RO");
	}
}

ZTEST_SUITE(jump_data, NULL, NULL, jump_data_before, NULL, NULL);
