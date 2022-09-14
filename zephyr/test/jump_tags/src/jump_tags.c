/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <setjmp.h>

#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "sysjump.h"
#include "system_fake.h"
#include "system.h"

#define TEST_BASIC_JUMP_TAG 0x9901
#define TEST_MISSING_JUMP_TAG 0x9902
#define TEST_MAX_JUMP_TAG 0x9903
#define TEST_TOO_BIG_JUMP_TAG 0x9904

#define TEST_JUMP_TAG_VERSION 1

#define SOME_STR_VAL "JumpTagTest"

void (*add_tag_func)(void);

struct test_basic_jump_data_struct {
	char some_str[32];
};

struct test_max_jump_data_struct {
	char some_str[JUMP_TAG_MAX_SIZE];
};

struct test_too_big_jump_data_struct {
	char some_str[JUMP_TAG_MAX_SIZE + 1];
};

static void system_before(void *data)
{
	add_tag_func = NULL;
	system_common_pre_init();
	system_set_shrspi_image_copy(EC_IMAGE_RO);
}

static void do_fake_sysjump(void)
{
	jmp_buf env;
	enum ec_image target_image = system_get_image_copy() == EC_IMAGE_RO ?
					     EC_IMAGE_RW :
					     EC_IMAGE_RO;

	if (!setjmp(env)) {
		system_fake_setenv(&env);
		system_run_image_copy(target_image);
		zassert_unreachable();
	}

	system_set_shrspi_image_copy(target_image);
	zassert_equal(system_get_image_copy(), target_image);
}

static void add_max_jump_tag(void)
{
	struct test_max_jump_data_struct max_tag = {
		.some_str = SOME_STR_VAL,
	};
	zassert_ok(system_add_jump_tag(TEST_MAX_JUMP_TAG, TEST_JUMP_TAG_VERSION,
				       sizeof(max_tag), &max_tag));
}

static void add_too_big_jump_tag(void)
{
	struct test_too_big_jump_data_struct too_big_tag = {
		.some_str = SOME_STR_VAL,
	};
	zassert_equal(system_add_jump_tag(TEST_TOO_BIG_JUMP_TAG,
					  TEST_JUMP_TAG_VERSION,
					  sizeof(too_big_tag), &too_big_tag),
		      EC_ERROR_INVAL);
}

static void add_too_many_jump_tags(void)
{
	int rv;
	struct test_max_jump_data_struct max_tag = {
		.some_str = SOME_STR_VAL,
	};
	/* Ensure at least one tag can be added, but not 10 */
	for (int i = 0; i < 10; i++) {
		rv = system_add_jump_tag(TEST_MAX_JUMP_TAG,
					 TEST_JUMP_TAG_VERSION, sizeof(max_tag),
					 &max_tag);
		if (rv != 0) {
			zassert_equal(rv, EC_ERROR_INVAL);
			zassert_true(i > 0);
			return;
		}
	}
	zassert_unreachable(
		"Adding too many jump tags failed to result in an error");
}

static void add_basic_jump_tag(void)
{
	struct test_basic_jump_data_struct basic_tag = {
		.some_str = SOME_STR_VAL,
	};
	zassert_ok(system_add_jump_tag(TEST_BASIC_JUMP_TAG,
				       TEST_JUMP_TAG_VERSION, sizeof(basic_tag),
				       &basic_tag));
}

static void test_sysjump_hook(void)
{
	if (add_tag_func)
		add_tag_func();
}
DECLARE_HOOK(HOOK_SYSJUMP, test_sysjump_hook, HOOK_PRIO_DEFAULT);

static void check_for_jump_tag(int jump_tag, int expected_size)
{
	int version;
	int size;
	const unsigned char *data;

	data = system_get_jump_tag(jump_tag, &version, &size);
	zassert_equal(size, expected_size);
	zassert_equal(version, TEST_JUMP_TAG_VERSION);
	zassert_equal(strcmp(data, SOME_STR_VAL), 0);
}

ZTEST(jump_tags, test_get_missing_jump_tag)
{
	int version;
	int size;
	struct test_jump_data_struct *data;

	data = (struct test_jump_data_struct *)system_get_jump_tag(
		TEST_MISSING_JUMP_TAG, &version, &size);
	zassert_equal(data, NULL);
}

ZTEST(jump_tags, test_add_max_jump_tag)
{
	add_tag_func = add_max_jump_tag;
	do_fake_sysjump();
	check_for_jump_tag(TEST_MAX_JUMP_TAG,
			   sizeof(struct test_max_jump_data_struct));
}

ZTEST(jump_tags, test_too_big_jump_tag)
{
	add_tag_func = add_too_big_jump_tag;
	do_fake_sysjump();
}

ZTEST(jump_tags, test_too_many_jump_tags)
{
	add_tag_func = add_too_many_jump_tags;
	do_fake_sysjump();
	check_for_jump_tag(TEST_MAX_JUMP_TAG,
			   sizeof(struct test_max_jump_data_struct));
}

ZTEST(jump_tags, test_add_basic_jump_tag)
{
	add_tag_func = add_basic_jump_tag;
	do_fake_sysjump();
	check_for_jump_tag(TEST_BASIC_JUMP_TAG,
			   sizeof(struct test_basic_jump_data_struct));
}

ZTEST_SUITE(jump_tags, NULL, NULL, system_before, NULL, NULL);
