/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test of the key=val variable implementation (set, get, delete, etc).
 */

#include <string.h>

#include "common.h"
#include "compile_time_macros.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "printf.h"
#include "shared_mem.h"
#include "test_util.h"

/* Declare the user regions (see test_config.h) */
uint32_t nvmem_user_sizes[] = {
	CONFIG_FLASH_NVMEM_VARS_USER_SIZE,
};
BUILD_ASSERT(ARRAY_SIZE(nvmem_user_sizes) == NVMEM_NUM_USERS);

/* Internal functions exported for test */
void release_local_copy(void);

/****************************************************************************/
/* Mock the flash storage */

static uint8_t ram_buffer[CONFIG_FLASH_NVMEM_VARS_USER_SIZE];
static uint8_t flash_buffer[CONFIG_FLASH_NVMEM_VARS_USER_SIZE];

int nvmem_read(uint32_t startOffset, uint32_t size,
	       void *data_, enum nvmem_users user)
{
	/* Our mocks make some assumptions */
	if (startOffset != 0 ||
	    size > CONFIG_FLASH_NVMEM_VARS_USER_SIZE ||
	    user != CONFIG_FLASH_NVMEM_VARS_USER_NUM)
		return EC_ERROR_UNIMPLEMENTED;

	if (!data_)
		return EC_ERROR_INVAL;

	memcpy(data_, flash_buffer, size);

	return EC_SUCCESS;
}

int nvmem_write(uint32_t startOffset, uint32_t size,
		void *data_, enum nvmem_users user)
{
	/* Our mocks make some assumptions */
	if (startOffset != 0 ||
	    size > CONFIG_FLASH_NVMEM_VARS_USER_SIZE ||
	    user != CONFIG_FLASH_NVMEM_VARS_USER_NUM)
		return EC_ERROR_UNIMPLEMENTED;

	if (!data_)
		return EC_ERROR_INVAL;

	memcpy(ram_buffer, data_, size);

	return EC_SUCCESS;
}

int nvmem_commit(void)
{
	memcpy(flash_buffer, ram_buffer, CONFIG_FLASH_NVMEM_VARS_USER_SIZE);
	return EC_SUCCESS;
}

/****************************************************************************/
/* Helper routines */

static void erase_flash(void)
{
	/* Invalidate the RAM cache */
	release_local_copy();

	/* Zero flash */
	memset(flash_buffer, 0xff, sizeof(flash_buffer));
}

/* Erase flash, then copy data_ over it */
static void load_flash(const uint8_t *data_, size_t data_len)
{
	erase_flash();
	memcpy(flash_buffer, data_, data_len);
}

/* Return true if flash matches data_, and is followed by 0xff to the end */
static int verify_flash(const uint8_t *data_, size_t data_len)
{
	size_t i;

	/* mismatch means false */
	if (memcmp(flash_buffer, data_, data_len))
		return 0;

	for (i = data_len;
	     i < CONFIG_FLASH_NVMEM_VARS_USER_SIZE - data_len;
	     i++)
		if (flash_buffer[i] != 0xff)
			return 0;
	return 1;
}

/*
 * Treating both as strings, save the <key, value> pair.
 */
int str_setvar(const char *key, const char *val)
{
	/* Only for tests, so assume the length will fit */
	uint8_t key_len, val_len;

	key_len = strlen(key);
	val_len = val ? strlen(val) : 0;

	return setvar(key, key_len, val, val_len);
}

/*
 * Treating both as strings, lookup the key and compare the result with the
 * expected value. Return true if they match.
 */
static int str_matches(const char *key, const char *expected_val)
{
	const struct tuple *t = getvar(key, strlen(key));
	uint8_t expected_len;

	if (!expected_val && !t)
		return 1;

	if (expected_val && !t)
		return 0;

	if (!expected_val && t)
		return 0;

	expected_len = strlen(expected_val);
	return !memcmp(tuple_val(t), expected_val, expected_len);
}

/****************************************************************************/
/* Tests */

static int check_init(void)
{
	/* Valid entries */
	const uint8_t good[] = { 0x01, 0x01, 0x00, 'A', 'a',
				 0x01, 0x01, 0x00, 'B', 'b',
				 0x00 };

	/* Empty variables are 0x00, followed by all 0xff */
	const uint8_t empty[] = { 0x00 };

	/*
	 * This is parsed as though there's only one variable, but it's wrong
	 * because the rest of the storage isn't 0xff.
	 */
	const uint8_t bad_key[] = { 0x01, 0x01, 0x00, 'A', 'a',
				    0x00, 0x01, 0x00, 'B', 'b',
				    0x00 };

	/* Zero-length variables are not allowed */
	const uint8_t bad_val[] = { 0x01, 0x01, 0x00, 'A', 'a',
				    0x01, 0x00, 0x00, 'B', 'b',
				    0x00 };

	/* The next constants use magic numbers based on on the region size */
	BUILD_ASSERT(CONFIG_FLASH_NVMEM_VARS_USER_SIZE == 600);

	/* This is one byte too large */
	const uint8_t too_big[] = { [0] = 0xff, [1] = 0xff, /* 0 - 512 */
				    [513] = 0x01, [514] = 0x53, /* 513 - 599 */
				    [599] = 0x00 };

	/* This should just barely fit */
	const uint8_t just_right[] = { [0] = 0xff, [1] = 0xff, /* 0-512 */
				       [513] = 0x01, [514] = 0x52, /* 513-598 */
				       [599] = 0x00 };

	/* No end marker */
	const uint8_t not_right[] = { [0] = 0xff, [1] = 0xff, /* 0-512 */
				      [513] = 0x01, [514] = 0x52, /* 513-598 */
				      [599] = 0xff };

	erase_flash();
	load_flash(good, sizeof(good));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(good, sizeof(good)));

	erase_flash();
	load_flash(empty, sizeof(empty));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	/* All 0xff quickly runs off the end of the storage */
	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	erase_flash();
	load_flash(bad_key, sizeof(bad_key));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	erase_flash();
	load_flash(bad_val, sizeof(bad_val));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	erase_flash();
	load_flash(too_big, sizeof(too_big));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	erase_flash();
	load_flash(just_right, sizeof(just_right));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(just_right, sizeof(just_right)));

	erase_flash();
	load_flash(not_right, sizeof(not_right));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	return EC_SUCCESS;
}

static int simple_search(void)
{
	const uint8_t preload[] = {
		0x02, 0x02, 0x00, 'h', 'o',  'y', 'o',
		0x02, 0x4,  0x00, 'y', 'o',  'h', 'o', 'y', 'o',
		0x02, 0x06, 0x00, 'm', 'o',  'y', 'o', 'h', 'o', 'y', 'o',
		0x00 };

	load_flash(preload, sizeof(preload));
	TEST_ASSERT(initvars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(preload, sizeof(preload)));

	TEST_ASSERT(str_matches("no", 0));
	TEST_ASSERT(str_matches("ho", "yo"));
	TEST_ASSERT(str_matches("yo", "hoyo"));
	TEST_ASSERT(str_matches("mo", "yohoyo"));

	return EC_SUCCESS;
}

static int simple_write(void)
{
	const uint8_t after_one[] = {
		0x02, 0x02, 0x00, 'h', 'o',  'y', 'o',
		0x00 };

	const uint8_t after_two[] = {
		0x02, 0x02, 0x00, 'h', 'o',  'y', 'o',
		0x02, 0x4,  0x00, 'y', 'o',  'h', 'o', 'y', 'o',
		0x00 };

	const uint8_t after_three[] = {
		0x02, 0x02, 0x00, 'h', 'o',  'y', 'o',
		0x02, 0x4,  0x00, 'y', 'o',  'h', 'o', 'y', 'o',
		0x02, 0x06, 0x00, 'm', 'o',  'y', 'o', 'h', 'o', 'y', 'o',
		0x00 };

	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);

	TEST_ASSERT(setvar("ho", 2, "yo", 2) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_one, sizeof(after_one)));

	TEST_ASSERT(setvar("yo", 2, "hoyo", 4) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_two, sizeof(after_two)));

	TEST_ASSERT(setvar("mo", 2, "yohoyo", 6) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_three, sizeof(after_three)));

	return EC_SUCCESS;
}

static int simple_delete(void)
{
	const char start_with[] = {
		0x01, 0x05, 0x00, 'A', 'a',  'a', 'a', 'a', 'a',
		0x02, 0x03, 0x00, 'B', 'B',  'b', 'b', 'b',
		0x03, 0x06, 0x00, 'C', 'C',  'C', 'x', 'y', 'z', 'p', 'd', 'q',
		0x01, 0x03, 0x00, 'M', 'm',  '0', 'm',
		0x04, 0x01, 0x00, 'N', 'N',  'N', 'N', 'n',
		0x00 };

	const char after_one[] = {
		0x02, 0x03, 0x00, 'B', 'B',  'b', 'b', 'b',
		0x03, 0x06, 0x00, 'C', 'C',  'C', 'x', 'y', 'z', 'p', 'd', 'q',
		0x01, 0x03, 0x00, 'M', 'm',  '0', 'm',
		0x04, 0x01, 0x00, 'N', 'N',  'N', 'N', 'n',
		0x00 };

	const char after_two[] = {
		0x02, 0x03, 0x00, 'B', 'B',  'b', 'b', 'b',
		0x03, 0x06, 0x00, 'C', 'C',  'C', 'x', 'y', 'z', 'p', 'd', 'q',
		0x01, 0x03, 0x00, 'M', 'm',  '0', 'm',
		0x00 };

	const char after_three[] = {
		0x02, 0x03, 0x00, 'B', 'B',  'b', 'b', 'b',
		0x01, 0x03, 0x00, 'M', 'm',  '0', 'm',
		0x00 };

	const char empty[] = { 0x00 };

	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);

	TEST_ASSERT(setvar("A", 1, "aaaaa", 5) == EC_SUCCESS);
	TEST_ASSERT(setvar("BB", 2, "bbb", 3) == EC_SUCCESS);
	TEST_ASSERT(setvar("CCC", 3, "xyzpdq", 6) == EC_SUCCESS);
	TEST_ASSERT(setvar("M", 1, "m0m", 3) == EC_SUCCESS);
	TEST_ASSERT(setvar("NNNN", 4, "n", 1) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(start_with, sizeof(start_with)));

	/* Zap first variable by setting var_len to 0 */
	TEST_ASSERT(setvar("A", 1, "yohoyo", 0) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_one, sizeof(after_one)));

	/* Zap last variable by passing null pointer */
	TEST_ASSERT(setvar("NNNN", 4, 0, 3) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_two, sizeof(after_two)));

	/* Ensure that zapping nonexistant variable does nothing */
	TEST_ASSERT(setvar("XXX", 3, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_two, sizeof(after_two)));

	/* Zap variable in the middle */
	TEST_ASSERT(setvar("CCC", 3, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(after_three, sizeof(after_three)));

	/* Zap the rest */
	TEST_ASSERT(setvar("BB", 2, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(setvar("M", 1, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	/* Zapping a nonexistant variable still does nothing */
	TEST_ASSERT(setvar("XXX", 3, 0, 0) == EC_SUCCESS);
	TEST_ASSERT(writevars() == EC_SUCCESS);
	TEST_ASSERT(verify_flash(empty, sizeof(empty)));

	return EC_SUCCESS;
}

static int complex_write(void)
{
	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);

	/* Do a bunch of writes and erases */
	str_setvar("ho", "aa");
	str_setvar("zo", "nn");
	str_setvar("yo", "CCCCCCCC");
	str_setvar("zooo", "yyyyyyy");
	str_setvar("yo", "AA");
	str_setvar("ho", 0);
	str_setvar("yi", "BBB");
	str_setvar("yi", "AA");
	str_setvar("hixx", 0);
	str_setvar("yo", "BBB");
	str_setvar("zo", "");
	str_setvar("hi", "bbb");
	str_setvar("ho", "cccccc");
	str_setvar("yo", "");
	str_setvar("zo", "ggggg");

	/* What do we expect to find? */
	TEST_ASSERT(str_matches("hi", "bbb"));
	TEST_ASSERT(str_matches("hixx", 0));
	TEST_ASSERT(str_matches("ho", "cccccc"));
	TEST_ASSERT(str_matches("yi", "AA"));
	TEST_ASSERT(str_matches("yo", 0));
	TEST_ASSERT(str_matches("zo", "ggggg"));
	TEST_ASSERT(str_matches("zooo", "yyyyyyy"));

	return EC_SUCCESS;
}

static int weird_keys(void)
{
	uint8_t keyA[255];
	uint8_t keyB[255];
	const char *valA = "this is A";
	const char *valB = "THIS IS b";
	int i;
	const struct tuple *t;

	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);

	for (i = 0; i < 255; i++) {
		keyA[i] = i;
		keyB[i] = 255 - i;
	}

	TEST_ASSERT(setvar(keyA, sizeof(keyA),
			   valA, strlen(valA)) == EC_SUCCESS);

	TEST_ASSERT(setvar(keyB, sizeof(keyB),
			   valB, strlen(valB)) == EC_SUCCESS);

	TEST_ASSERT(writevars() == EC_SUCCESS);

	t = getvar(keyA, sizeof(keyA));
	TEST_ASSERT(t);
	TEST_ASSERT(t->val_len == strlen(valA));
	TEST_ASSERT(memcmp(tuple_val(t), valA, strlen(valA)) == 0);

	t = getvar(keyB, sizeof(keyB));
	TEST_ASSERT(t);
	TEST_ASSERT(t->val_len == strlen(valB));
	TEST_ASSERT(memcmp(tuple_val(t), valB, strlen(valB)) == 0);

	return EC_SUCCESS;
}

static int weird_values(void)
{
	const char *keyA = "this is A";
	const char *keyB = "THIS IS b";
	char valA[255];
	char valB[255];
	int i;
	const struct tuple *t;

	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);

	for (i = 0; i < 255; i++) {
		valA[i] = i;
		valB[i] = 255 - i;
	}

	TEST_ASSERT(setvar(keyA, strlen(keyA),
			   valA, sizeof(valA)) == EC_SUCCESS);
	TEST_ASSERT(str_setvar("c", "CcC") == EC_SUCCESS);
	TEST_ASSERT(setvar(keyB, strlen(keyB),
			   valB, sizeof(valB)) == EC_SUCCESS);
	TEST_ASSERT(str_setvar("d", "dDd") == EC_SUCCESS);

	TEST_ASSERT(writevars() == EC_SUCCESS);

	t = getvar(keyA, strlen(keyA));
	TEST_ASSERT(t);
	TEST_ASSERT(memcmp(tuple_val(t), valA, sizeof(valA)) == 0);

	t = getvar(keyB, strlen(keyB));
	TEST_ASSERT(t);
	TEST_ASSERT(memcmp(tuple_val(t), valB, sizeof(valB)) == 0);

	TEST_ASSERT(str_matches("c", "CcC"));
	TEST_ASSERT(str_matches("d", "dDd"));

	return EC_SUCCESS;
}

static int fill_it_up(void)
{
	int i, n;
	char key[20];

	erase_flash();
	TEST_ASSERT(initvars() == EC_SUCCESS);

	/*
	 * Some magic numbers here, because we want to use up 10 bytes at a
	 * time and end up with exactly 9 free bytes left.
	 */
	TEST_ASSERT(CONFIG_FLASH_NVMEM_VARS_USER_SIZE % 10 == 0);
	n = CONFIG_FLASH_NVMEM_VARS_USER_SIZE / 10;
	TEST_ASSERT(n < 1000);

	/* Fill up the storage */
	for (i = 0; i < n - 1; i++) {
		/* 3-byte header, 5-char key, 2-char val, == 10 chars */
		snprintf(key, sizeof(key), "kk%03d", i);
		TEST_ASSERT(setvar(key, 5, "aa", 2) == EC_SUCCESS);
	}

	/*
	 * Should be nine bytes left in rbuf (because we need one more '\0' at
	 * the end). This won't fit.
	 */
	TEST_ASSERT(setvar("kk999", 5, "aa", 2) == EC_ERROR_OVERFLOW);
	/* But this will. */
	TEST_ASSERT(setvar("kk999", 5, "a", 1) == EC_SUCCESS);
	/* And this, because it replaces a previous entry */
	TEST_ASSERT(setvar("kk000", 5, "bc", 2) == EC_SUCCESS);
	/* But this still won't fit */
	TEST_ASSERT(setvar("kk999", 5, "de", 2) == EC_ERROR_OVERFLOW);

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(check_init);
	RUN_TEST(simple_write);
	RUN_TEST(simple_search);
	RUN_TEST(simple_delete);
	RUN_TEST(complex_write);
	RUN_TEST(weird_keys);
	RUN_TEST(weird_values);
	RUN_TEST(fill_it_up);

	test_print_result();
}
