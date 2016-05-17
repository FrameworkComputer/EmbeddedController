/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test Cr-50 Non-Voltatile memory module
 */

#include "common.h"
#include "console.h"
#include "crc.h"
#include "nvmem.h"
#include "flash.h"
#include "shared_mem.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define WRITE_SEGMENT_LEN 200
#define WRITE_READ_SEGMENTS 4

uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
	NVMEM_USER_0_SIZE,
	NVMEM_USER_1_SIZE,
	NVMEM_USER_2_SIZE
};

static uint8_t write_buffer[NVMEM_PARTITION_SIZE];
static uint8_t read_buffer[NVMEM_PARTITION_SIZE];
static int flash_write_fail;

void nvmem_compute_sha(uint8_t *p_buf, int num_bytes, uint8_t *p_sha,
		       int sha_bytes)
{
	uint32_t crc;
	uint32_t *p_data;
	int n;

	crc32_init();
	/* Assuming here that buffer is 4 byte aligned and that num_bytes is
	 * divisible by 4
	 */
	p_data = (uint32_t *)p_buf;
	for (n = 0; n < num_bytes/4; n++)
		crc32_hash32(*p_data++);
	crc = crc32_result();

	p_data = (uint32_t *)p_sha;
	*p_data = crc;
}

/* Used to allow/prevent Flash erase/write operations */
int flash_pre_op(void)
{
	return flash_write_fail ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static int generate_random_data(int offset, int num_bytes)
{
	int m, n, limit;
	uint32_t r_data;

	/* Ensure it will fit in the write buffer */
	TEST_ASSERT((num_bytes + offset) <= NVMEM_PARTITION_SIZE);
	/* Seed random number sequence */
	r_data = prng((uint32_t)clock());
	m = 0;
	while (m < num_bytes) {
		r_data = prng(r_data);
		limit = MIN(4, num_bytes - m);
		/* No byte alignment assumptions */
		for (n = 0; n < limit; n++)
			write_buffer[offset + m + n] = (r_data >> (n*8)) & 0xff;
		m += limit;
	}

	return EC_SUCCESS;
}

static int test_write_read(uint32_t offset, uint32_t num_bytes, int user)
{
	int ret;

	/* Generate source data */
	generate_random_data(0, num_bytes);
	/* Write source data to NvMem */
	ret = nvmem_write(offset, num_bytes, write_buffer, user);
	if (ret)
		return ret;
	nvmem_read(offset, num_bytes, read_buffer, user);
	/* Verify memory was written into cache ram buffer */
	TEST_ASSERT_ARRAY_EQ(write_buffer, read_buffer, num_bytes);
	/* Write to flash */
	ret = nvmem_commit();
	if (ret != EC_SUCCESS)
		return ret;
	/* Read from flash */
	nvmem_read(offset, num_bytes, read_buffer, user);
	/* Verify that write to flash was successful */
	TEST_ASSERT_ARRAY_EQ(write_buffer, read_buffer, num_bytes);

	return EC_SUCCESS;
}

static int write_full_buffer(uint32_t size, int user)
{
	uint32_t offset;
	uint32_t len;
	int ret;

	/* Start at beginning of the user buffer */
	offset = 0;
	do {
		/* User default segment length unless it will exceed */
		len = MIN(WRITE_SEGMENT_LEN, size - offset);
		/* Generate data for tx buffer */
		generate_random_data(offset, len);
		/* Write data to Nvmem cache memory */
		ret = nvmem_write(offset, len, &write_buffer[offset], user);
		if (ret != EC_SUCCESS)
			return ret;
		/* Write to flash */
		ret = nvmem_commit();
		if (ret != EC_SUCCESS)
			return ret;
		/* Adjust starting offset by segment length */
		offset += len;
	} while (offset < size);

	/* Entire flash buffer should be full at this point */
	nvmem_read(0, size, read_buffer, user);
	/* Verify that write to flash was successful */
	TEST_ASSERT_ARRAY_EQ(write_buffer, read_buffer, size);

	return EC_SUCCESS;
}

static int test_fully_erased_nvmem(void)
{
	/*
	 * The purpose of this test is to check NvMem intialization when NvMem
	 * is completely erased (i.e. following SpiFlash write of program). In
	 * this configuration, nvmem_init() should be able to detect this case
	 * and configure an initial NvMem partition.
	 */

	/* Erase full NvMem area */
	flash_physical_erase(CONFIG_FLASH_NVMEM_OFFSET,
			     CONFIG_FLASH_NVMEM_SIZE);
	/* Call NvMem initialization function */
	return nvmem_init();
}

static int test_configured_nvmem(void)
{
	/*
	 * The purpose of this test is to check nvmem_init() when both
	 * partitions are configured and valid.
	 */

	/* Configure all NvMem partitions with starting version number 0 */
	nvmem_setup(0);
	/* Call NvMem initialization */
	return nvmem_init();
}

static int test_corrupt_nvmem(void)
{
	uint32_t offset;
	int n;

	/*
	 * The purpose of this test is to check nvmem_init() in the case when no
	 * vailid partition exists (not fully erased and no valid sha). In this
	 * case, NvMem can't be initialized and should return an error to the
	 * calling function.
	 */

	/* Overwrite tags of each parition */
	memset(write_buffer, 0, 8);
	for (n = 0; n < NVMEM_NUM_PARTITIONS; n++) {
		offset = NVMEM_PARTITION_SIZE * n;
		flash_physical_write(CONFIG_FLASH_NVMEM_OFFSET + offset, 8,
				     (const char *)write_buffer);
	}
	/* In this case nvmem_init is expected to fail */
	return !nvmem_init();
}

static int test_write_read_sequence(void)
{
	uint32_t offset;
	uint32_t length;
	int user;
	int n;
	int ret;

	for (user = 0; user < NVMEM_NUM_USERS; user++) {
		/* Length for each write/read segment */
		length = nvmem_user_sizes[user] / WRITE_READ_SEGMENTS;
		/* Start at beginning of user buffer */
		offset = 0;
		for (n = 0; n < WRITE_READ_SEGMENTS; n++) {
			ret = test_write_read(offset, length, user);
			if (ret != EC_SUCCESS)
				return ret;
			/* Adjust offset by segment length */
			offset += length;
			/* For 1st iteration only, adjust to create stagger */
			if (n == 0)
				offset -= length / 2;

		}
	}
	return EC_SUCCESS;
}

static int test_write_full_multi(void)
{
	int n;
	int ret;

	/*
	 * The purpose of this test is to completely fill each user buffer in
	 * NvMem with random data a segment length at a time. The data written
	 * to NvMem is saved in write_buffer[] and then can be used to check the
	 * NvMem writes were successful by reading and then comparing each user
	 * buffer.
	 */
	for (n = 0; n < NVMEM_NUM_USERS; n++) {
		ret = write_full_buffer(nvmem_user_sizes[n], n);
		if (ret != EC_SUCCESS)
			return ret;
	}
	return EC_SUCCESS;
}

static int test_write_fail(void)
{
	uint32_t offset = 0;
	uint32_t num_bytes = 0x200;
	int ret;

	/* Do write/read sequence that's expected to be successful */
	if (test_write_read(offset, num_bytes, NVMEM_USER_0))
		return EC_ERROR_UNKNOWN;

	/* Prevent flash erase/write operations */
	flash_write_fail = 1;
	/* Attempt flash write */
	ret = test_write_read(offset, num_bytes, NVMEM_USER_0);
	/* Resume normal operation */
	flash_write_fail = 0;

	/* This test is successful if write attempt failed */
	return !ret;
}

static int test_cache_not_available(void)
{
	char **p_shared;
	int ret;
	uint32_t offset = 0;
	uint32_t num_bytes = 0x200;

	/*
	 * The purpose of this test is to validate that NvMem writes behave as
	 * expected when the shared memory buffer (used for cache ram) is and
	 * isn't available.
	 */

	/* Do write/read sequence that's expected to be successful */
	if (test_write_read(offset, num_bytes, NVMEM_USER_1))
		return EC_ERROR_UNKNOWN;

	/* Acquire shared memory */
	if (shared_mem_acquire(num_bytes, p_shared))
		return EC_ERROR_UNKNOWN;

	/* Attempt write/read sequence that should fail */
	ret = test_write_read(offset, num_bytes, NVMEM_USER_1);
	/* Release shared memory */
	shared_mem_release(*p_shared);
	if (!ret)
		return EC_ERROR_UNKNOWN;

	/* Write/read sequence should work now */
	return test_write_read(offset, num_bytes, NVMEM_USER_1);
}

static int test_buffer_overflow(void)
{
	int ret;
	int n;

	/*
	 * The purpose of this test is to check that NvMem writes behave
	 * properly in relation to the defined length of each user buffer. A
	 * write operation to completely fill the buffer is done first. This
	 * should pass. Then the same buffer is written to with one extra byte
	 * and this operation is expected to fail.
	 */

	/* Do test for each user buffer */
	for (n = 0; n < NVMEM_NUM_USERS; n++) {
		/* Write full buffer */
		ret = write_full_buffer(nvmem_user_sizes[n], n);
		if (ret != EC_SUCCESS)
			return ret;
		/* Attempt to write full buffer plus 1 extra byte */
		ret = write_full_buffer(nvmem_user_sizes[n] + 1, n);
		if (!ret)
			return EC_ERROR_UNKNOWN;
	}

	/* Test case where user buffer number is valid */
	ret = test_write_read(0, 0x100, NVMEM_USER_0);
	if (ret != EC_SUCCESS)
		return ret;
	/* Attempt same write, but with invalid user number */
	ret = test_write_read(0, 0x100, NVMEM_NUM_USERS);
	if (!ret)
		return ret;

	return EC_SUCCESS;
}

static void run_test_setup(void)
{
	/* Allow Flash erase/writes */
	flash_write_fail = 0;
	test_reset();
}

void run_test(void)
{
	run_test_setup();
	/* Test NvMem Initialization function */
	RUN_TEST(test_corrupt_nvmem);
	RUN_TEST(test_fully_erased_nvmem);
	RUN_TEST(test_configured_nvmem);
	/* Test Read/Write/Commit functions */
	RUN_TEST(test_write_read_sequence);
	RUN_TEST(test_write_full_multi);
	/* Test flash erase/write fail case */
	RUN_TEST(test_write_fail);
	/* Test shared_mem not available case */
	RUN_TEST(test_cache_not_available);
	/* Test buffer overflow logic */
	RUN_TEST(test_buffer_overflow);
	test_print_result();
}
