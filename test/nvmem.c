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
static int lock_test_started;

int app_cipher(const void *salt_p, void *out_p, const void *in_p, size_t size)
{

	const uint8_t *in = in_p;
	uint8_t *out = out_p;
	const uint8_t *salt = salt_p;
	size_t i;

	for (i = 0; i < size; i++)
		out[i] = in[i] ^ salt[i % CIPHER_SALT_SIZE];

	return 1;
}

void app_compute_hash(uint8_t *p_buf, size_t num_bytes,
		      uint8_t *p_hash, size_t hash_bytes)
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

	for (n = 0; n < hash_bytes; n += sizeof(crc)) {
		size_t copy_bytes = MIN(sizeof(crc), hash_bytes - n);

		memcpy(p_hash + n, &crc, copy_bytes);
	}
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
		nvmem_write(offset, len, &write_buffer[offset], user);
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
	flash_physical_erase(CONFIG_FLASH_NVMEM_OFFSET_A,
			     NVMEM_PARTITION_SIZE);
	flash_physical_erase(CONFIG_FLASH_NVMEM_OFFSET_B,
			     NVMEM_PARTITION_SIZE);
	/* Call NvMem initialization function */
	return nvmem_init();
}

static int test_configured_nvmem(void)
{
	/*
	 * The purpose of this test is to check nvmem_init() when both
	 * partitions are configured and valid.
	 */

	/* Call NvMem initialization */
	return nvmem_init();
}

/* Verify that nvmem_erase_user_data only erases the given user's data. */
static int test_nvmem_erase_user_data(void)
{
	uint32_t write_value;
	uint32_t read_value;
	int i;

	nvmem_init();

	/* Make sure all partitions have data in them. */
	for (i = 0; i < NVMEM_NUM_PARTITIONS; i++) {
		write_value = i;
		nvmem_write(0, sizeof(write_value), &write_value, NVMEM_USER_0);
		write_value = 2;
		nvmem_write(0, sizeof(write_value), &write_value, NVMEM_USER_1);
		write_value = 3;
		nvmem_write(0, sizeof(write_value), &write_value, NVMEM_USER_2);
		nvmem_commit();
	}

	/* Check that the writes took place. */
	read_value = ~write_value;
	nvmem_read(0, sizeof(read_value), &read_value, NVMEM_USER_0);
	TEST_ASSERT(read_value == i-1);
	nvmem_read(0, sizeof(read_value), &read_value, NVMEM_USER_1);
	TEST_ASSERT(read_value == 2);
	nvmem_read(0, sizeof(read_value), &read_value, NVMEM_USER_2);
	TEST_ASSERT(read_value == 3);

	/*
	 * nvmem_erase_user_data() is supposed to erase the user's data across
	 * all partitions.
	 */
	nvmem_erase_user_data(NVMEM_USER_0);

	for (i = 0; i < NVMEM_NUM_PARTITIONS; i++) {
		/* Make sure USER 0's data is (still) gone. */
		nvmem_read(0, sizeof(read_value), &read_value, NVMEM_USER_0);
		TEST_ASSERT(read_value == 0xffffffff);

		/* Make sure the other users' data has been untouched. */
		nvmem_read(0, sizeof(read_value), &read_value, NVMEM_USER_1);
		TEST_ASSERT(read_value == 2);

		/*
		 * The active partition changes when the contents of the cache
		 * changes.  Therefore, in order to examine all the paritions,
		 * we'll keep modifying one of the user's data.
		 */
		nvmem_read(0, sizeof(read_value), &read_value, NVMEM_USER_2);
		TEST_ASSERT(read_value == (3+i));
		write_value = 4 + i;
		nvmem_write(0, sizeof(write_value), &write_value, NVMEM_USER_2);
		nvmem_commit();
	}

	return EC_SUCCESS;
}

static int test_corrupt_nvmem(void)
{
	uint8_t invalid_value = 0x55;
	int ret;
	struct nvmem_tag *p_part;
	uint8_t *p_data;

	/*
	 * The purpose of this test is to check nvmem_init() in the case when no
	 * vailid partition exists (not fully erased and no valid sha). In this
	 * case, the initialization create one new valid partition.
	 */

	/* Overwrite each partition will all 0s */
	memset(write_buffer, invalid_value, NVMEM_PARTITION_SIZE);
	flash_physical_write(CONFIG_FLASH_NVMEM_OFFSET_A,
				     NVMEM_PARTITION_SIZE,
				     (const char *)write_buffer);
	flash_physical_write(CONFIG_FLASH_NVMEM_OFFSET_B,
				     NVMEM_PARTITION_SIZE,
				     (const char *)write_buffer);
	/*
	 * The initialization function will look for a valid partition and if
	 * none is found, it will create one, and save it at partition index
	 * 1.
	 */
	ret = nvmem_init();
	if (ret)
		return ret;

	/*
	 * nvmem_init() called on uninitialized flash will create the first
	 * valid partition with generation set to 0 at flash partition 1.
	 *
	 * Check here that partition 1 has a generation number of 0.
	 */
	p_part = (struct nvmem_tag *)CONFIG_FLASH_NVMEM_BASE_B;
	TEST_ASSERT(p_part->generation == 0);
	p_data = (uint8_t *)p_part + sizeof(struct nvmem_tag);

	/* Verify that partition 0 is still empty. */
	memset(write_buffer, invalid_value, NVMEM_PARTITION_SIZE);
	p_data = (void *)CONFIG_FLASH_NVMEM_BASE_A;
	TEST_ASSERT_ARRAY_EQ(write_buffer, p_data, NVMEM_PARTITION_SIZE);

	/* Now let's write a different value  into user NVMEM_CR50 */
	invalid_value ^= ~0;
	TEST_ASSERT(nvmem_write(0, sizeof(invalid_value),
				&invalid_value, NVMEM_USER_0) == EC_SUCCESS);
	TEST_ASSERT(nvmem_commit() == EC_SUCCESS);

	/* Verify that partition 1 generation did not change. */
	TEST_ASSERT(p_part->generation == 0);

	/*
	 * Now verify that partition 0 generation is set to 1;
	 */
	p_part = (struct nvmem_tag *)CONFIG_FLASH_NVMEM_BASE_A;
	TEST_ASSERT(p_part->generation == 1);

	return EC_SUCCESS;
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

static int test_move(void)
{
	uint32_t len = 0x100;
	uint32_t nv1_offset;
	uint32_t nv2_offset;
	int user = 0;
	int n;
	int ret;

	/*
	 * The purpose of this test is to check that nvmem_move() behaves
	 * properly. This test only uses one user buffer as accessing multiple
	 * user buffers is tested separately. This test uses writes a set of
	 * test data then test move operations with full overlap, half overlap
	 * and no overlap. Folliwng these tests, the boundary conditions for
	 * move operations are checked for the giver user buffer.
	 */

	nv1_offset = 0;
	for (n = 0; n < 3; n++) {
		/* Generate Test data */
		generate_random_data(nv1_offset, len);
		nv2_offset = nv1_offset + (len / 2) * n;
		/* Write data to Nvmem cache memory */
		nvmem_write(nv1_offset, len, &write_buffer[nv1_offset], user);
		nvmem_commit();
		/* Test move while data is in cache area */
		nvmem_move(nv1_offset, nv2_offset, len, user);
		nvmem_read(nv2_offset, len, read_buffer, user);
		if (memcmp(write_buffer, read_buffer, len))
			return EC_ERROR_UNKNOWN;
		ccprintf("Memmove nv1 = 0x%x, nv2 = 0x%x\n",
			 nv1_offset, nv2_offset);
	}
	/* Test invalid buffer offsets */
	/* Destination offset is equal to length of buffer */
	nv1_offset = 0;
	nv2_offset = nvmem_user_sizes[user];
	/* Attempt to move just 1 byte */
	ret = nvmem_move(nv1_offset, nv2_offset, 1, user);
	if (!ret)
		return EC_ERROR_UNKNOWN;

	/* Source offset is equal to length of buffer */
	nv1_offset = nvmem_user_sizes[user];
	nv2_offset = 0;
	/* Attempt to move just 1 byte */
	ret = nvmem_move(nv1_offset, nv2_offset, 1, user);
	if (!ret)
		return EC_ERROR_UNKNOWN;

	nv1_offset = 0;
	nv2_offset = nvmem_user_sizes[user] - len;
	/* Move data chunk from start to end of buffer */
	ret = nvmem_move(nv1_offset, nv2_offset,
			 len, user);
	if (ret)
		return ret;

	/* Attempt to move data chunk 1 byte beyond end of user buffer */
	nv1_offset = 0;
	nv2_offset = nvmem_user_sizes[user] - len + 1;
	ret = nvmem_move(nv1_offset, nv2_offset,
			 len, user);
	if (!ret)
		return EC_ERROR_UNKNOWN;
	/* nvmem_move returned an error, need to clear internal error state */
	nvmem_commit();

	return EC_SUCCESS;
}

static int test_is_different(void)
{
	uint32_t len = 0x41;
	uint32_t nv1_offset = 0;
	int user = 1;
	int ret;

	/*
	 * The purpose of this test is to verify nv_is_different(). Test data is
	 * written to a location in user buffer 1, then a case that's expected
	 * to pass along with a case that is expected to fail are checked. Next
	 * the same tests are repeated when the NvMem write is followed by a
	 * commit operation.
	 */

	/* Generate test data */
	generate_random_data(nv1_offset, len);
	/* Write to NvMem cache buffer */
	nvmem_write(nv1_offset, len, &write_buffer[nv1_offset], user);
	/* Expected to be the same */
	ret = nvmem_is_different(nv1_offset, len,
				 &write_buffer[nv1_offset], user);
	if (ret)
		return EC_ERROR_UNKNOWN;

	/* Expected to be different */
	ret = nvmem_is_different(nv1_offset + 1, len,
				 &write_buffer[nv1_offset], user);
	if (!ret)
		return EC_ERROR_UNKNOWN;

	/* Commit cache buffer and retest */
	nvmem_commit();
	/* Expected to be the same */
	ret = nvmem_is_different(nv1_offset, len,
				 &write_buffer[nv1_offset], user);
	if (ret)
		return EC_ERROR_UNKNOWN;

	/* Expected to be different */
	write_buffer[nv1_offset] ^= 0xff;
	ret = nvmem_is_different(nv1_offset, len,
				 &write_buffer[nv1_offset], user);
	if (!ret)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int nvmem_first_task(void *unused)
{
	uint32_t offset = 0;
	uint32_t num_bytes = WRITE_SEGMENT_LEN;
	int user = NVMEM_USER_0;

	task_wait_event(0);
	/* Generate source data */
	generate_random_data(0, num_bytes);
	nvmem_write(0, num_bytes, &write_buffer[offset], user);
	/* Read from cache memory */
	nvmem_read(0, num_bytes, read_buffer, user);
	/* Verify that write to nvmem was successful */
	TEST_ASSERT_ARRAY_EQ(write_buffer, read_buffer, num_bytes);
	/* Wait here with mutex held by this task */
	task_wait_event(0);
	/* Write to flash which releases nvmem mutex */
	nvmem_commit();
	nvmem_read(0, num_bytes, read_buffer, user);
	/* Verify that write to flash was successful */
	TEST_ASSERT_ARRAY_EQ(write_buffer, read_buffer, num_bytes);

	return EC_SUCCESS;
}

int nvmem_second_task(void *unused)
{
	uint32_t offset = WRITE_SEGMENT_LEN;
	uint32_t num_bytes = WRITE_SEGMENT_LEN;
	int user = NVMEM_USER_0;

	task_wait_event(0);

	/* Gen test data and don't overwite test data generated by 1st task */
	generate_random_data(offset, num_bytes);
	/* Write test data at offset 0 nvmem user buffer */
	nvmem_write(0, num_bytes, &write_buffer[offset], user);
	/* Write to flash */
	nvmem_commit();
	/* Read from nvmem */
	nvmem_read(0, num_bytes, read_buffer, user);
	/* Verify that write to nvmem was successful */
	TEST_ASSERT_ARRAY_EQ(&write_buffer[offset], read_buffer, num_bytes);
	/* Clear flag to indicate lock test is complete */
	lock_test_started = 0;

	return EC_SUCCESS;
}

static int test_lock(void)
{
	/*
	 * This purpose of this test is to verify the mutex lock portion of the
	 * nvmem module. There are two additional tasks utilized. The first task
	 * is woken and it creates some test data and does an
	 * nvmem_write(). This will cause the mutex to be locked by the 1st
	 * task. The 1st task then waits and control is returned to this
	 * function and the 2nd task is woken, the 2nd task also attempts to
	 * write data to nvmem. The 2nd task should stall waiting for the mutex
	 * to be unlocked.
	 *
	 * When control returns to this function, the 1st task is woken again
	 * and the nvmem operation is completed. This will allow the 2nd task to
	 * grab the lock and finish its nvmem operation. The test will not
	 * complete until the 2nd task finishes the nvmem write. A static global
	 * flag is used to let this function know when the 2nd task is complete.
	 *
	 * Both tasks write to the same location in nvmem so the test will only
	 * pass if the 2nd task can't write until the nvmem write in the 1st
	 * task is completed.
	 */

	/* Set flag for start of test */
	lock_test_started = 1;
	/* Wake first_task */
	task_wake(TASK_ID_NV_1);
	task_wait_event(1000);
	/* Wake second_task. It should stall waiting for mutex */
	task_wake(TASK_ID_NV_2);
	task_wait_event(1000);
	/* Go back to first_task so it can complete its nvmem operation */
	task_wake(TASK_ID_NV_1);
	/* Wait for 2nd task to complete nvmem operation */
	while (lock_test_started)
		task_wait_event(100);

	return EC_SUCCESS;
}

static int test_nvmem_save(void)
{
	/*
	 * The purpose of this test is to verify that if the written value
	 * did not change the cache contents there is no actual write
	 * happening at the commit time.
	 */
	int dummy_value;
	int offset = 0x10;
	uint8_t generation_a;
	uint8_t generation_b;
	uint8_t prev_generation;
	uint8_t new_generation;
	const struct nvmem_tag *part_a;
	const struct nvmem_tag *part_b;
	const struct nvmem_tag *new_gen_part;
	const struct nvmem_tag *prev_gen_part;

	part_a = (const struct nvmem_tag *)CONFIG_FLASH_NVMEM_BASE_A;
	part_b = (const struct nvmem_tag *)CONFIG_FLASH_NVMEM_BASE_B;
	/*
	 * Make sure nvmem is initialized and both partitions have been
	 * written.
	 */
	nvmem_init();

	/*
	 * Make sure something is changed at offset 0x10 into the second user
	 * space.
	 */
	nvmem_read(offset, sizeof(dummy_value), &dummy_value, NVMEM_USER_1);
	dummy_value ^= ~0;
	nvmem_write(0x10, sizeof(dummy_value), &dummy_value, NVMEM_USER_1);
	nvmem_commit();

	/* Verify that the two generation values are different. */
	generation_a = part_a->generation;
	generation_b = part_b->generation;
	TEST_ASSERT(generation_a != generation_b);

	/*
	 * Figure out which one should change next, we are close to the
	 * beginnig of the test, no wrap is expected.
	 */
	if (generation_a > generation_b) {
		prev_generation = generation_a;
		new_generation = generation_a + 1;
		new_gen_part = part_b;
		prev_gen_part = part_a;
	} else {
		prev_generation = generation_b;
		new_generation = generation_b + 1;
		new_gen_part = part_a;
		prev_gen_part = part_b;
	}

	/* Write a new value, this should trigger generation switch. */
	dummy_value += 1;
	TEST_ASSERT(nvmem_write(0x10, sizeof(dummy_value),
				&dummy_value, NVMEM_USER_1) == EC_SUCCESS);
	TEST_ASSERT(nvmem_commit() == EC_SUCCESS);

	TEST_ASSERT(prev_gen_part->generation == prev_generation);
	TEST_ASSERT(new_gen_part->generation == new_generation);

	/* Write the same value, this should NOT trigger generation switch. */
	TEST_ASSERT(nvmem_write(0x10, sizeof(dummy_value),
				&dummy_value, NVMEM_USER_1) == EC_SUCCESS);
	TEST_ASSERT(nvmem_commit() == EC_SUCCESS);

	TEST_ASSERT(prev_gen_part->generation == prev_generation);
	TEST_ASSERT(new_gen_part->generation == new_generation);

	return EC_SUCCESS;
}

static void run_test_setup(void)
{
	/* Allow Flash erase/writes */
	flash_write_fail = 0;
	test_reset();
}

int DCRYPTO_ladder_is_enabled(void)
{
	return 1;
}

void run_test(void)
{
	run_test_setup();
	RUN_TEST(test_corrupt_nvmem);
	RUN_TEST(test_fully_erased_nvmem);
	RUN_TEST(test_configured_nvmem);
	RUN_TEST(test_write_read_sequence);
	RUN_TEST(test_write_full_multi);
	RUN_TEST(test_write_fail);
	RUN_TEST(test_buffer_overflow);
	RUN_TEST(test_move);
	RUN_TEST(test_is_different);
	RUN_TEST(test_lock);
	RUN_TEST(test_nvmem_erase_user_data);
	RUN_TEST(test_nvmem_save);
	test_print_result();
}
