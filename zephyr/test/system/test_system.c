/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <logging/log.h>
#include "ec_commands.h"
#include "system.h"
#include "sysjump.h"
LOG_MODULE_REGISTER(test);

#define JUMP_TAG_TOTAL_SIZE 512

struct jump_memory {
	uint8_t jump_tag_memory[JUMP_TAG_TOTAL_SIZE];
	struct jump_data jdata;
};

static struct jump_memory jump_memory;

static void setup(void)
{
	system_common_reset_state();
	memset(&jump_memory, 0, sizeof(struct jump_memory));
}

static void test_set_reset_flags(void)
{
	zassert_equal(system_get_reset_flags(), 0,
		      "system_get_reset_flags() should be 0 at the start");
	system_set_reset_flags(EC_RESET_FLAG_OTHER);
	zassert_equal(
		system_get_reset_flags(), EC_RESET_FLAG_OTHER,
		"system_get_reset_flags() should match exactly to EC_RESET_FLAG_OTHER");
}

static void test_clear_reset_flags(void)
{
	uint32_t flags = EC_RESET_FLAG_OTHER | EC_RESET_FLAG_STAY_IN_RO;

	system_set_reset_flags(flags);
	zassert_not_equal(system_get_reset_flags(), 0,
			  "system_get_reset_flags() should be non-zero");
	/* Remove the reset hard flag. */
	system_clear_reset_flags(EC_RESET_FLAG_OTHER);
	zassert_equal(system_get_reset_flags(), EC_RESET_FLAG_STAY_IN_RO,
		      "system_get_reset_flags() should have removed "
		      "EC_RESET_FLAG_OTHER after reset.");
}

static void test_encode_save_flags_preserve(void)
{
	const uint32_t expected_flags = EC_RESET_FLAG_OTHER |
					EC_RESET_FLAG_USB_RESUME |
					EC_RESET_FLAG_EFS;
	uint32_t save_flags;

	system_set_reset_flags(expected_flags);

	/*
	 * Preserve the existing flags, should add EC_RESET_FLAG_PRESERVED and
	 * EC_RESET_FLAG_SOFT.
	 */
	system_encode_save_flags(SYSTEM_RESET_PRESERVE_FLAGS, &save_flags);
	zassert_equal(save_flags,
		      expected_flags | EC_RESET_FLAG_PRESERVED |
			      EC_RESET_FLAG_SOFT,
		      "All the reset flags should have been restored.");
}

static void test_encode_save_flags_translate_system_to_ec(void)
{
	uint32_t save_flags;

	system_encode_save_flags(SYSTEM_RESET_LEAVE_AP_OFF, &save_flags);
	zassert_equal(
		save_flags, EC_RESET_FLAG_AP_OFF | EC_RESET_FLAG_SOFT,
		"Expected save flags to be EC_RESET_FLAG_AP_OFF | EC_RESET_FLAG_SOFT");

	system_encode_save_flags(SYSTEM_RESET_STAY_IN_RO, &save_flags);
	zassert_equal(
		save_flags, EC_RESET_FLAG_STAY_IN_RO | EC_RESET_FLAG_SOFT,
		"Expected save flags to be EC_RESET_FLAG_STAY_IN_RO | EC_RESET_FLAG_SOFT");

	system_encode_save_flags(SYSTEM_RESET_HARD, &save_flags);
	zassert_equal(save_flags, EC_RESET_FLAG_HARD,
		      "Expected save flags to be EC_RESET_FLAG_HARD");

	system_encode_save_flags(SYSTEM_RESET_WAIT_EXT, &save_flags);
	zassert_equal(save_flags, EC_RESET_FLAG_HARD,
		      "Expected save flags to be EC_RESET_FLAG_HARD");
}

static void test_common_pre_init_fail_matching_magic_number(void)
{
	/* Put garbage values in test_jdata. */
	jump_memory.jdata.struct_size = sizeof(struct jump_data);
	jump_memory.jdata.reset_flags = 0xff;
	jump_memory.jdata.version = 3;
	jump_memory.jdata.magic = 12345;

	system_override_jdata(&jump_memory.jdata);
	system_common_pre_init();

	/* Verify that test_jdata was zeroed out. */
	for (size_t i = 0; i < sizeof(struct jump_data); ++i) {
		zassert_equal(((uint8_t *)&jump_memory.jdata)[i], 0,
			      "Expecting byte %d of jdata to be 0.", i);
	}
}

static void test_common_pre_init_with_delta_struct_size(void)
{
	/* Set the old struct size to be 1 smaller than the current one. */
	jump_memory.jdata.struct_size = sizeof(struct jump_data) - 1;
	jump_memory.jdata.version = JUMP_DATA_VERSION;
	jump_memory.jdata.magic = JUMP_DATA_MAGIC;
	jump_memory.jdata.jump_tag_total = JUMP_TAG_TOTAL_SIZE;

	/*
	 * Since we're telling the system component that the size is 1 smaller
	 * than it really is it should calculate that the delta is 1 and shift
	 * all the tags by 1 byte to the left.
	 */
	jump_memory.jump_tag_memory[1] = 0xff;

	system_override_jdata(&jump_memory.jdata);
	system_common_pre_init();

	zassert_equal(
		jump_memory.jump_tag_memory[0], 0xff,
		"Expected byte 0 to have the value from previous position 1 in "
		"jump tag memory");
	zassert_equal(jump_memory.jump_tag_memory[1], 0,
		      "Expected byte 1 to have moved to position 0 in jump tag "
		      "memory");
}

static void test_common_pre_init_resets_jdata_not_jump_tags(void)
{
	jump_memory.jdata.struct_size = sizeof(struct jump_data);
	jump_memory.jdata.version = JUMP_DATA_VERSION;
	jump_memory.jdata.magic = JUMP_DATA_MAGIC;
	jump_memory.jdata.jump_tag_total = JUMP_TAG_TOTAL_SIZE;
	jump_memory.jdata.reserved0 = 0xf0;

	for (size_t i = 0; i < JUMP_TAG_TOTAL_SIZE; ++i)
		jump_memory.jump_tag_memory[i] = i & 0xff;

	system_override_jdata(&jump_memory.jdata);
	system_common_pre_init();

	zassert_equal(jump_memory.jdata.jump_tag_total, 0,
		      "Expected jump_tag_total to be reset to 0");
	zassert_equal(jump_memory.jdata.struct_size, sizeof(struct jump_data),
		      "Expected struct_size to match sizeof(struct jump_data)");
	zassert_equal(jump_memory.jdata.reserved0, 0,
		      "Expected the reseved field to be reset to 0");
	zassert_equal(jump_memory.jdata.magic, 0,
		      "Expected the magic number to be reset to 0");

	for (size_t i = 0; i < JUMP_TAG_TOTAL_SIZE; ++i) {
		zassert_equal(
			jump_memory.jump_tag_memory[i], i & 0xff,
			"Expected jump_tag_memory[%d] to remain unchanged.", i);
	}
}

static void test_add_jump_tag_fail_no_init(void)
{
	zassert_equal(
		system_add_jump_tag(0, 0, 0, NULL), -EINVAL,
		"Can't set a jump tag without calling common_pre_init first.");

	system_override_jdata(&jump_memory.jdata);
	system_common_pre_init();
	zassert_equal(system_add_jump_tag(0, 0, 0, NULL), -EINVAL,
		      "Can't set a jump tag without valid jdata.");
}

static void test_add_jump_tag_fail_size_out_of_bounds(void)
{
	system_override_jdata(&jump_memory.jdata);
	system_common_pre_init();
	jump_memory.jdata.magic = JUMP_DATA_MAGIC;

	zassert_equal(system_add_jump_tag(0, 0, -1, NULL), -EINVAL,
		      "Can't set jump tag with negative size");
	zassert_equal(system_add_jump_tag(0, 0, 256, NULL), -EINVAL,
		      "Can't set jump tag with size > 255");
}

static void test_add_jump_tag(void)
{
	const uint16_t data = 0x1234;
	uint16_t tag = 0;
	int version = 1;
	const uint8_t *returned_data;
	int returned_size;

	system_override_jdata(&jump_memory.jdata);
	system_common_pre_init();
	jump_memory.jdata.magic = JUMP_DATA_MAGIC;

	zassert_equal(system_add_jump_tag(tag, version, sizeof(uint16_t),
					  &data),
		      0, "Expected add_jump_tag to return 0");

	returned_data = system_get_jump_tag(tag, &version, &returned_size);
	zassert_not_null(returned_data, "Failed to get tag data for tag <%u>",
			 tag);
	zassert_equal(version, 1, "Expected version to be 1 but got <%d>",
		      version);
	zassert_equal(returned_size, sizeof(uint16_t),
		      "Expected returned size to be %u but got <%u>",
		      sizeof(uint16_t), returned_size);
	zassert_equal(*((uint16_t *)returned_data), data,
		      "Expected returned data to be %u but got <%u>", data,
		      *((uint16_t *)returned_data));
}

void test_main(void)
{
	ztest_test_suite(
		system,
		ztest_unit_test_setup_teardown(test_set_reset_flags, setup,
					       unit_test_noop),
		ztest_unit_test_setup_teardown(test_clear_reset_flags, setup,
					       unit_test_noop),
		ztest_unit_test_setup_teardown(test_encode_save_flags_preserve,
					       setup, unit_test_noop),
		ztest_unit_test_setup_teardown(
			test_encode_save_flags_translate_system_to_ec, setup,
			unit_test_noop),
		ztest_unit_test_setup_teardown(
			test_common_pre_init_fail_matching_magic_number, setup,
			unit_test_noop),
		ztest_unit_test_setup_teardown(
			test_common_pre_init_with_delta_struct_size, setup,
			unit_test_noop),
		ztest_unit_test_setup_teardown(
			test_common_pre_init_resets_jdata_not_jump_tags, setup,
			unit_test_noop),
		ztest_unit_test_setup_teardown(test_add_jump_tag_fail_no_init,
					       setup, unit_test_noop),
		ztest_unit_test_setup_teardown(
			test_add_jump_tag_fail_size_out_of_bounds, setup,
			unit_test_noop),
		ztest_unit_test_setup_teardown(test_add_jump_tag, setup,
					       unit_test_noop));
	ztest_run_test_suite(system);
}
