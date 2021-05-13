/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test ec version
 */

#include "common.h"
#include "ec_commands.h"
#include "stddef.h"
#include "system.h"
#include "util.h"
#include "test_util.h"

/*
 * Tests that fw version adheres to the expected format.
 * Example fw version: host_v2.0.10135+b3e38e380c
 */
static int test_version(void)
{
	const char *fw_version;
	size_t board_name_length, major_version_length, minor_version_length,
		sub_minor_version_length, hash_length;
	const char *major_version_ptr, *minor_version_ptr,
		*sub_minor_version_ptr, *hash_ptr;

	fw_version = system_get_version(EC_IMAGE_RO);

	TEST_ASSERT(fw_version != NULL);

	ccprintf("fw_version: %s\n", fw_version);

	TEST_LE(strlen(fw_version), (size_t)32, "%zu");

	board_name_length = strcspn(fw_version, "_");

	TEST_GE(board_name_length, (size_t)3, "%zu");

	major_version_ptr = fw_version + board_name_length + 1;
	major_version_length = strcspn(major_version_ptr, ".");

	TEST_GE(major_version_length, (size_t)2, "%zu");
	TEST_EQ(major_version_ptr[0], 'v', "%c");
	for (int i = 1; i < major_version_length; i++)
		TEST_ASSERT(isdigit(major_version_ptr[i]));

	minor_version_ptr = major_version_ptr + major_version_length + 1;
	minor_version_length = strcspn(minor_version_ptr, ".");

	TEST_GE(minor_version_length, (size_t)1, "%zu");
	for (int i = 0; i < minor_version_length; i++)
		TEST_ASSERT(isdigit(minor_version_ptr[i]));

	sub_minor_version_ptr = minor_version_ptr + minor_version_length + 1;
	sub_minor_version_length = strcspn(sub_minor_version_ptr, "-+");

	TEST_GE(sub_minor_version_length, (size_t)1, "%zu");
	for (int i = 0; i < sub_minor_version_length; i++)
		TEST_ASSERT(isdigit(sub_minor_version_ptr[i]));

	hash_ptr = sub_minor_version_ptr + sub_minor_version_length + 1;
	hash_length = strlen(hash_ptr);

	TEST_GE(hash_length, (size_t)8, "%zu");
	for (int i = 0; i < hash_length; i++)
		TEST_ASSERT(isdigit(hash_ptr[i]) ||
			    (hash_ptr[i] >= 'a' && hash_ptr[i] <= 'f'));

	return EC_SUCCESS;
}

/*
 * Tests that cros fwid adheres to the expected format.
 * Example cros fwid: host_14175.0.21_08_24
 */
static int test_fwid(void)
{
	const char *cros_fwid;
	size_t board_name_length, major_version_length, minor_version_length,
		sub_minor_version_length;
	const char *major_version_ptr, *minor_version_ptr,
		*sub_minor_version_ptr;

	cros_fwid = system_get_cros_fwid(EC_IMAGE_RO);

	TEST_ASSERT(cros_fwid != NULL);

	ccprintf("cros_fwid: %s\n", cros_fwid);

	TEST_LE(strlen(cros_fwid), (size_t)32, "%zu");

	board_name_length = strcspn(cros_fwid, "_");
	TEST_GE(board_name_length, (size_t)3, "%zu");

	major_version_ptr = cros_fwid + board_name_length + 1;
	major_version_length = strcspn(major_version_ptr, ".");
	TEST_GE(major_version_length, (size_t)5, "%zu");

	for (int i = 0; i < major_version_length; i++)
		TEST_ASSERT(isdigit(major_version_ptr[i]));

	minor_version_ptr = major_version_ptr + major_version_length + 1;
	minor_version_length = strcspn(minor_version_ptr, ".");
	TEST_GE(minor_version_length, (size_t)1, "%zu");

	for (int i = 0; i < minor_version_length; i++)
		TEST_ASSERT(isdigit(minor_version_ptr[i]));

	sub_minor_version_ptr = minor_version_ptr + minor_version_length + 1;
	sub_minor_version_length = strlen(sub_minor_version_ptr);
	TEST_GE(sub_minor_version_length, (size_t)1, "%zu");

	for (int i = 0; i < sub_minor_version_length; i++)
		TEST_ASSERT(isdigit(sub_minor_version_ptr[i]) ||
			    sub_minor_version_ptr[i] == '_');

	return EC_SUCCESS;
}

/*
 * Tests requesting TEST.
 * Example fw version: host_v2.0.10135+b3e38e380c
 */
static int test_image_unknown(void)
{
	const char *fw_version;
	const char *cros_fwid;

	fw_version = system_get_version(EC_IMAGE_UNKNOWN);

	TEST_ASSERT(fw_version != NULL);
	TEST_LE(strlen(fw_version), (size_t)32, "%zu");

	cros_fwid = system_get_cros_fwid(EC_IMAGE_UNKNOWN);

	TEST_ASSERT(cros_fwid != NULL);
	TEST_LE(strlen(cros_fwid), (size_t)32, "%zu");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_version);
	RUN_TEST(test_fwid);
	RUN_TEST(test_image_unknown);

	test_print_result();
}
