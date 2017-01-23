/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "nvmem.h"
#include "shared_mem.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ## args)
#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ## args)

#define NVMEM_ACQUIRE_CACHE_SLEEP_MS 25
#define NVMEM_ACQUIRE_CACHE_MAX_ATTEMPTS (250 / NVMEM_ACQUIRE_CACHE_SLEEP_MS)
#define NVMEM_NOT_INITIALIZED (-1)

/* Table of start addresses for each partition */
static const uintptr_t nvmem_base_addr[NVMEM_NUM_PARTITIONS] = {
		CONFIG_FLASH_NVMEM_BASE_A,
		CONFIG_FLASH_NVMEM_BASE_B
	};

/* NvMem user buffer start offset table */
static uint32_t nvmem_user_start_offset[NVMEM_NUM_USERS];

/* A/B partion that is most up to date */
static int nvmem_act_partition;

/* NvMem cache memory structure */
struct nvmem_cache {
	uint8_t *base_ptr;
	task_id_t task;
	struct mutex mtx;
};

struct nvmem_cache cache;

static uint8_t commits_enabled;
static uint8_t commits_skipped;

/* NvMem error state */
static int nvmem_error_state;
/* Flag to track if an Nv write/move is not completed */
static int nvmem_write_error;

/*
 * Given the nvmem tag address calculate the sha value of the nvmem buffer and
 * save it in the provided space. The caller is expected to provide enough
 * space to store CIPHER_SALT_SIZE bytes.
 */
static void nvmem_compute_sha(struct nvmem_tag *tag, void *sha_buf)
{
	app_compute_hash(tag->padding, NVMEM_PARTITION_SIZE - NVMEM_SHA_SIZE,
			 sha_buf, sizeof(tag->sha));
}

static int nvmem_save(uint8_t tag_generation)
{
	struct nvmem_tag *tag;
	size_t nvmem_offset;
	int dest_partition = (nvmem_act_partition + 1) % NVMEM_NUM_PARTITIONS;

	/* Flash offset of the partition to save. */
	nvmem_offset = nvmem_base_addr[dest_partition] -
		CONFIG_PROGRAM_MEMORY_BASE;

	/* Erase partition */
	if (flash_physical_erase(nvmem_offset,
				 NVMEM_PARTITION_SIZE)) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return EC_ERROR_UNKNOWN;
	}

	tag = (struct nvmem_tag *)cache.base_ptr;
	tag->generation = tag_generation;
	tag->layout_version = NVMEM_LAYOUT_VERSION;

	/* Calculate sha of the whole thing. */
	nvmem_compute_sha(tag, tag->sha);

	/* Encrypt actual payload. */
	if (!app_cipher(tag->sha, tag + 1, tag + 1,
			NVMEM_PARTITION_SIZE - sizeof(struct nvmem_tag))) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return EC_ERROR_UNKNOWN;
	}

	/* Write partition */
	if (flash_physical_write(nvmem_offset,
				 NVMEM_PARTITION_SIZE,
				 cache.base_ptr)) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return EC_ERROR_UNKNOWN;
	}

	nvmem_act_partition = dest_partition;
	return EC_SUCCESS;
}

/*
 * Read from flash and verify partition.
 *
 * @param index - index of the partition to verify
 * @param part_buffer - if non-null, a pointer where the caller wants to save
 *                      the address of the verified partition in SRAM. If
 *                      verification succeeded, the caller is responsible for
 *                      releasing the allocated memory.
 *
 * Returns EC_SUCCESS on verification success
 *         EC_ERROR_BUSY in case of malloc failure
 *         EC_ERROR_UNKNOWN on failure to decrypt of verify.
 */
static int nvmem_partition_read_verify(int index, void **part_buffer)
{
	uint8_t sha_comp[NVMEM_SHA_SIZE];
	struct nvmem_partition *p_part;
	struct nvmem_partition *p_copy;
	int ret;

	p_part = (struct nvmem_partition *)nvmem_base_addr[index];

	/* First copy it into ram. */
	ret = shared_mem_acquire(NVMEM_PARTITION_SIZE, (char **)&p_copy);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s failed to malloc!\n", __func__);
		return ret;
	}
	memcpy(p_copy, p_part, NVMEM_PARTITION_SIZE);

	/* Then decrypt it. */
	if (!app_cipher(p_copy->tag.sha, &p_copy->tag + 1,
			&p_copy->tag + 1,
			NVMEM_PARTITION_SIZE - sizeof(struct nvmem_tag))) {
		CPRINTF("%s: decryption failure\n", __func__);
		shared_mem_release(p_copy);
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Check if computed value matches stored value. Nonzero 'ret' value
	 * means there was a match.
	 */
	nvmem_compute_sha(&p_copy->tag, sha_comp);
	ret = !memcmp(p_copy->tag.sha, sha_comp, NVMEM_SHA_SIZE);

	if (ret && part_buffer)
		*part_buffer = p_copy;

	if (!ret || !part_buffer)
		shared_mem_release(p_copy);

	return ret ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

/* Called with the semaphore locked. */
static int nvmem_acquire_cache(void)
{
	int attempts = 0;

	cache.base_ptr = NULL;  /* Just in case. */

	while (attempts < NVMEM_ACQUIRE_CACHE_MAX_ATTEMPTS) {
		int ret;

		ret = nvmem_partition_read_verify(nvmem_act_partition,
						  (void **)&cache.base_ptr);

		if (ret != EC_ERROR_BUSY)
			return ret;

		CPRINTF("Shared Mem not available on attempt %d!\n", attempts);
		/* TODO: what time really makes sense? */
		msleep(NVMEM_ACQUIRE_CACHE_SLEEP_MS);
		attempts++;
	}
	/* Timeout Error condition */
	CPRINTF("%s:%d\n", __func__, __LINE__);
	return EC_ERROR_TIMEOUT;
}

static int nvmem_lock_cache(void)
{
	/*
	 * Need to protect the cache contents and pointer value from other tasks
	 * attempting to do nvmem write operations. However, since this function
	 * may be called mutliple times prior to the mutex lock being released,
	 * there is a check first to see if the current task holds the lock. If
	 * it does then the task number will equal the value in cache.task. If
	 * the lock is held by a different task then mutex_lock function will
	 * operate as normal.
	 */
	if (cache.task != task_get_current()) {
		mutex_lock(&cache.mtx);
		cache.task = task_get_current();
	} else
		/* Lock is held by current task, nothing else to do. */
		return EC_SUCCESS;

	/*
	 * Acquire the shared memory buffer and copy the full
	 * partition size from flash into the cache buffer.
	 */
	if (nvmem_acquire_cache() != EC_SUCCESS) {
		/* Shared memory not available, need to release lock */
		/* Set task number to value that can't match a valid task */
		cache.task = TASK_ID_COUNT;
		/* Release lock */
		mutex_unlock(&cache.mtx);
		return EC_ERROR_TIMEOUT;
	}

	return EC_SUCCESS;
}

static void nvmem_release_cache(void)
{
	/* Done with shared memory buffer, release it. */
	shared_mem_release(cache.base_ptr);
	/* Inidicate cache is not available */
	cache.base_ptr = NULL;
	/* Reset task number to max value */
	cache.task = TASK_ID_COUNT;
	/* Release mutex lock here */
	mutex_unlock(&cache.mtx);
}

static int nvmem_reinitialize(void)
{
	int ret;

	/*
	 * NvMem is not properly initialized. Let's just erase everything and
	 * start over, so that at least 1 partition is ready to be used.
	 */
	nvmem_act_partition = 0;

	/* Need to acquire the shared memory buffer */
	ret = shared_mem_acquire(NVMEM_PARTITION_SIZE,
				 (char **)&cache.base_ptr);

	if (ret != EC_SUCCESS)
		return ret;

	memset(cache.base_ptr, 0xff, NVMEM_PARTITION_SIZE);

	/* Start with generation zero in the current active partition. */
	ret = nvmem_save(0);
	shared_mem_release(cache.base_ptr);
	cache.base_ptr = 0;
	if (ret != EC_SUCCESS)
		CPRINTF("%s:%d\n", __func__, __LINE__);
	return ret;
}

static int nvmem_compare_generation(void)
{
	struct nvmem_partition *p_part;
	uint16_t ver0, ver1;
	uint32_t delta;

	p_part = (struct nvmem_partition *)nvmem_base_addr[0];
	ver0 = p_part->tag.generation;
	p_part = (struct nvmem_partition *)nvmem_base_addr[1];
	ver1 = p_part->tag.generation;

	/* Compute generation difference accounting for wrap condition */
	delta = (ver0 - ver1 + (1<<NVMEM_GENERATION_BITS)) &
		NVMEM_GENERATION_MASK;
	/*
	 * If generation number delta is positive in a circular sense then
	 * partition 0 has the newest generation number. Otherwise, it's
	 * partition 1.
	 */
	return delta < (1<<(NVMEM_GENERATION_BITS-1)) ? 0 : 1;
}

static int nvmem_find_partition(void)
{
	int n;

	/* Don't know which partition to use yet */
	nvmem_act_partition = NVMEM_NOT_INITIALIZED;
	/*
	 * Check each partition to determine if the sha is good. If both
	 * partitions have valid sha(s), then compare generation numbers to
	 * select the most recent one.
	 */
	for (n = 0; n < NVMEM_NUM_PARTITIONS; n++)
		if (nvmem_partition_read_verify(n, NULL) == EC_SUCCESS) {
			if (nvmem_act_partition == NVMEM_NOT_INITIALIZED)
				nvmem_act_partition = n;
			else
				nvmem_act_partition =
					nvmem_compare_generation();
		} else {
			ccprintf("%s:%d partiton %d verification FAILED\n",
				 __func__, __LINE__, n);
		}

	if (nvmem_act_partition != NVMEM_NOT_INITIALIZED)
		return EC_SUCCESS;

	/*
	 * If active_partition is still not selected, then neither partition
	 * is valid. Let's reinitialize the NVMEM - there is nothing else we
	 * can do.
	 */
	CPRINTS("%s: No Valid Partition found, will reinitialize!", __func__);

	if (nvmem_reinitialize() != EC_SUCCESS) {
		CPRINTS("%s: Reinitialization failed!!");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int nvmem_generate_offset_table(void)
{
	int n;
	uint32_t start_offset;

	/*
	 * Create table of starting offsets within partition for each user
	 * buffer that's been defined.
	 */
	start_offset = sizeof(struct nvmem_tag);
	for (n = 0; n < NVMEM_NUM_USERS; n++) {
		nvmem_user_start_offset[n] = start_offset;
		start_offset += nvmem_user_sizes[n];
	}
	/* Verify that all defined user buffers fit within the partition */
	if (start_offset > NVMEM_PARTITION_SIZE)
		return EC_ERROR_OVERFLOW;

	return EC_SUCCESS;
}
static int nvmem_get_partition_off(int user, uint32_t offset,
				   uint32_t len, uint32_t *p_buf_offset)
{
	uint32_t start_offset;

	/* Sanity check for user */
	if (user >= NVMEM_NUM_USERS)
		return EC_ERROR_OVERFLOW;

	/* Get offset within the partition for the start of user buffer */
	start_offset = nvmem_user_start_offset[user];
	/*
	 * Ensure that read/write operation that is calling this function
	 * doesn't exceed the end of its buffer.
	 */
	if (offset + len > nvmem_user_sizes[user])
		return EC_ERROR_OVERFLOW;
	/* Compute offset within the partition for the rd/wr operation */
	*p_buf_offset = start_offset + offset;

	return EC_SUCCESS;
}

int nvmem_setup(uint8_t starting_generation)
{
	int part;
	int ret;

	CPRINTS("Configuring NVMEM Flash Partition");

	part = nvmem_act_partition;
	nvmem_act_partition = 0;

	/* Get the cache buffer */
	if (nvmem_lock_cache() != EC_SUCCESS) {
		CPRINTF("%s: Cache ram not available!\n", __func__);
		nvmem_act_partition = part;
		return EC_ERROR_TIMEOUT;
	}

	ret = EC_SUCCESS;

	for (part = 0; part < NVMEM_NUM_PARTITIONS; part++) {
		int rv;

		memset(cache.base_ptr, 0xff, NVMEM_PARTITION_SIZE);
		rv = nvmem_save(starting_generation + part);

		/* Even if one partition saving failed, let's keep going. */
		if (rv != EC_SUCCESS)
			ret = rv;
	}

	nvmem_release_cache();
	return ret;
}

int nvmem_init(void)
{
	int ret;

	/* Generate start offsets within partiion for user buffers */
	ret = nvmem_generate_offset_table();
	if (ret) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return ret;
	}
	/* Initialize error state, assume everything is good */
	nvmem_error_state = EC_SUCCESS;
	nvmem_write_error = 0;
	/* Default state for cache base_ptr and task number */
	cache.base_ptr = NULL;
	cache.task = TASK_ID_COUNT;

	ret = nvmem_find_partition();
	if (ret != EC_SUCCESS) {
		/* Change error state to non-zero */
		nvmem_error_state = ret;
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return ret;
	}

	CPRINTS("Active Nvmem partition set to %d", nvmem_act_partition);
	commits_enabled = 1;
	commits_skipped = 0;

	return EC_SUCCESS;
}

int nvmem_get_error_state(void)
{
	return nvmem_error_state;
}

int nvmem_is_different(uint32_t offset, uint32_t size, void *data,
		       enum nvmem_users user)
{
	int ret;
	uint32_t src_offset;
	int need_to_release;

	/* Point to either NvMem flash or ram if that's active */
	if (!cache.base_ptr) {
		ret = nvmem_lock_cache();
		if (ret != EC_SUCCESS)
			return ret;
		need_to_release = 1;
	} else {
		need_to_release = 0;
	}

	/* Get partition offset for this read operation */
	ret = nvmem_get_partition_off(user, offset, size, &src_offset);
	if (ret != EC_SUCCESS)
		return ret;

	/* Advance to the correct byte within the data buffer */

	/* Compare NvMem with data */
	ret = memcmp(cache.base_ptr + src_offset, data, size);
	if (need_to_release)
		nvmem_release_cache();

	return ret;
}

int nvmem_read(uint32_t offset, uint32_t size,
		    void *data, enum nvmem_users user)
{
	int ret;
	uint32_t src_offset;
	int need_to_release;

	if (!cache.base_ptr) {
		ret = nvmem_lock_cache();
		if (ret != EC_SUCCESS)
			return ret;
		need_to_release = 1;
	} else {
		need_to_release = 0;
	}

	/* Get partition offset for this read operation */
	ret = nvmem_get_partition_off(user, offset, size, &src_offset);

	if (ret == EC_SUCCESS)
		/* Copy from src into the caller's destination buffer */
		memcpy(data, cache.base_ptr + src_offset, size);

	if (commits_enabled && need_to_release)
		nvmem_release_cache();

	return ret;
}

int nvmem_write(uint32_t offset, uint32_t size,
		 void *data, enum nvmem_users user)
{
	int ret;
	uint8_t *p_dest;
	uintptr_t dest_addr;
	uint32_t dest_offset;

	/* Make sure that the cache buffer is active */
	ret = nvmem_lock_cache();
	if (ret) {
		nvmem_write_error = 1;
		return ret;
	}

	/* Compute partition offset for this write operation */
	ret = nvmem_get_partition_off(user, offset, size, &dest_offset);
	if (ret != EC_SUCCESS) {
		nvmem_write_error = 1;
		return ret;
	}

	/* Advance to correct offset within data buffer */
	dest_addr = (uintptr_t)cache.base_ptr;
	dest_addr += dest_offset;
	p_dest = (uint8_t *)dest_addr;
	/* Copy data from caller into destination buffer */
	memcpy(p_dest, data, size);

	return EC_SUCCESS;
}

int nvmem_move(uint32_t src_offset, uint32_t dest_offset, uint32_t size,
		enum nvmem_users user)
{
	int ret;
	uint8_t *p_src, *p_dest;
	uintptr_t base_addr;
	uint32_t s_buff_offset, d_buff_offset;

	/* Make sure that the cache buffer is active */
	ret = nvmem_lock_cache();
	if (ret) {
		nvmem_write_error = 1;
		return ret;
	}

	/* Compute partition offset for source */
	ret = nvmem_get_partition_off(user, src_offset, size, &s_buff_offset);
	if (ret != EC_SUCCESS) {
		nvmem_write_error = 1;
		return ret;
	}

	/* Compute partition offset for destination */
	ret = nvmem_get_partition_off(user, dest_offset, size, &d_buff_offset);
	if (ret != EC_SUCCESS) {
		nvmem_write_error = 1;
		return ret;
	}

	base_addr = (uintptr_t)cache.base_ptr;
	/* Create pointer to src location within partition */
	p_src = (uint8_t *)(base_addr + s_buff_offset);
	/* Create pointer to dest location within partition */
	p_dest = (uint8_t *)(base_addr + d_buff_offset);
	/* Move the data block in NvMem */
	memmove(p_dest, p_src, size);

	return EC_SUCCESS;
}

void nvmem_enable_commits(void)
{
	if (commits_enabled)
		return;

	commits_enabled = 1;
	if (!commits_skipped)
		return;

	CPRINTS("Committing NVMEM changes.");
	nvmem_commit();
	commits_skipped = 0;
}

void nvmem_disable_commits(void)
{
	commits_enabled = 0;
	commits_skipped = 0;
}

int nvmem_commit(void)
{
	uint16_t generation;
	struct nvmem_partition *p_part;

	if (!commits_enabled) {
		commits_skipped = 1;
		CPRINTS("Skipping commit");
		return EC_SUCCESS;
	}

	/* Ensure that all writes/moves prior to commit call succeeded */
	if (nvmem_write_error) {
		CPRINTS("NvMem: Write Error, commit abandoned");
		/* Clear error state */
		nvmem_write_error = 0;
		nvmem_release_cache();
		return EC_ERROR_UNKNOWN;
	}
	/*
	 * All scratch buffer blocks must be written to physical flash
	 * memory. In addition, the scratch block buffer index table
	 * entries must be reset along with the index itself.
	 */

	/* Update generation number */
	if (cache.base_ptr == NULL) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return EC_ERROR_UNKNOWN;
	}
	p_part = (struct nvmem_partition *)cache.base_ptr;
	generation = p_part->tag.generation + 1;
	/* Check for restricted generation number */
	if (generation == NVMEM_GENERATION_MASK)
		generation = 0;

	/* Write active partition to NvMem */
	if (nvmem_save(generation) != EC_SUCCESS) {
		/* Free up scratch buffers */
		nvmem_release_cache();
		return EC_ERROR_UNKNOWN;
	}

	/* Free up scratch buffers */
	nvmem_release_cache();
	return EC_SUCCESS;
}
