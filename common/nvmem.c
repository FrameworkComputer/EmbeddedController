/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "dcrypto.h"
#include "flash.h"
#include "nvmem.h"
#include "new_nvmem.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/*
 * The NVMEM contents are stored in flash memory. At run time there is an SRAM
 * cache and two instances of the contents in the flash in two partitions.
 *
 * Each instance is protected by a 16 bytes hash and has a 'generation' value
 * associated with it. When NVMEM module is initialized it checks the flash
 * stored instances. If both of them are valid, it considers the newer one
 * (younger generation) to be the proper NVMEM contents and copies it to the
 * SRAM cache. If only one instance is valid, it is used, and if no instances
 * are valid - a new valid partition is created and copied into the SRAM
 * cache.
 *
 * When stored in flash, the contents are encrypted, the hash value is used as
 * the IV for the encryption routine.
 *
 * There is a mutex controlling access to the NVMEM. There are two levels
 * of protection - for read only accesses and for write accesses. When the
 * module is initialized the mutex is opened.
 *
 * If there are no pending writes, each read access locks the mutex, reads out
 * the data and unlocks the mutex, thus multiple tasks could be reading NVMEM,
 * blocking access momentarily.
 *
 * If a write access ever occurs things get more complicated. The write access
 * leaves the mutex locked and stores the flag, indicating that the
 * contents have changed and need to be saved, and stores the task id of the
 * task performing the write access.
 *
 * The mutex remains locked in this case. Next time a read access happens,
 * if it comes from the same task, the unlock in the end of the read is
 * bypassed because the 'write in progress' flag is set. If a read or write
 * request comes from another task, they  will be blocked until the first
 * task to write commits.
 *
 * nvmem_commit() calls the nvmem_save() function which checks if the cache
 * contents indeed changed (by calculating the hash again). If there is no
 * change - the mutex is released and the function exits. If there is a
 * change, the new generation value is set, the new hash is calculated
 * and the copy is saved in the least recently used flash partition, and
 * then the lock is released.
 */

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
struct nvmem_mutex_ {
	task_id_t task;
	int write_in_progress;
	struct mutex mtx;
};

static struct nvmem_mutex_ nvmem_mutex = { .task = TASK_ID_COUNT };
static uint8_t nvmem_cache[NVMEM_PARTITION_SIZE] __aligned(4);

static uint8_t commits_enabled;

/* NvMem error state */
static int nvmem_error_state;
/* Flag to track if an Nv write/move is not completed */
static int nvmem_write_error;

static void nvmem_release_cache(void);

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

static int nvmem_save(void)
{
	enum ec_error_list rv;

	rv = new_nvmem_save();

	if (rv == EC_SUCCESS)
		nvmem_act_partition = NVMEM_NOT_INITIALIZED;

	nvmem_mutex.write_in_progress = 0;
	nvmem_release_cache();
	return rv;
}

/*
 * Read from flash and verify partition.
 *
 * @param index - index of the partition to verify
 *
 * Returns EC_SUCCESS on verification success
 *         EC_ERROR_BUSY in case of malloc failure
 *         EC_ERROR_UNKNOWN on failure to decrypt of verify.
 */
static int nvmem_partition_read_verify(int index)
{
	uint8_t sha_comp[NVMEM_SHA_SIZE];
	struct nvmem_partition *p_part;
	struct nvmem_partition *p_copy;
	int ret;

	p_part = (struct nvmem_partition *)nvmem_base_addr[index];
	p_copy = (struct nvmem_partition *)nvmem_cache;
	memcpy(p_copy, p_part, NVMEM_PARTITION_SIZE);

	/* Then decrypt it. */
	if (!app_cipher(p_copy->tag.sha, &p_copy->tag + 1,
			&p_copy->tag + 1,
			NVMEM_PARTITION_SIZE - sizeof(struct nvmem_tag))) {
		CPRINTF("%s: decryption failure\n", __func__);
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Check if computed value matches stored value. Nonzero 'ret' value
	 * means there was a match.
	 */
	nvmem_compute_sha(&p_copy->tag, sha_comp);
	ret = !memcmp(p_copy->tag.sha, sha_comp, NVMEM_SHA_SIZE);

	return ret ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static void nvmem_lock_cache(void)
{
	/*
	 * Need to protect the cache contents value from other tasks
	 * attempting to do nvmem write operations. However, since this
	 * function may be called mutliple times prior to the mutex lock being
	 * released, there is a check first to see if the current task holds
	 * the lock. If it does then the task number will equal the value in
	 * cache.task, no need to wait.
	 *
	 * If the lock is held by a different task then mutex_lock function
	 * will operate as normal.
	 */
	if (nvmem_mutex.task == task_get_current())
		return;

	mutex_lock(&nvmem_mutex.mtx);
	nvmem_mutex.task = task_get_current();
}

static void nvmem_release_cache(void)
{
	if (nvmem_mutex.write_in_progress || !commits_enabled)
		return;		/* It will have to be saved first. */

	/* Reset task number to max value */
	nvmem_mutex.task = TASK_ID_COUNT;
	/* Release mutex lock here */
	mutex_unlock(&nvmem_mutex.mtx);
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
	int newest;

	/* Don't know which partition to use yet */
	nvmem_act_partition = NVMEM_NOT_INITIALIZED;

	/* Find the newest partition available in flash. */
	newest = nvmem_compare_generation();

	/*
	 * Find a partition with a valid sha, starting with the newest one.
	 */
	for (n = 0; n < NVMEM_NUM_PARTITIONS; n++) {
		int check_part = (n + newest) % NVMEM_NUM_PARTITIONS;

		if (nvmem_partition_read_verify(check_part) == EC_SUCCESS) {
			nvmem_act_partition = check_part;
			ccprintf("%s:%d found legacy partition %d\n", __func__,
				 __LINE__, check_part);
			return EC_SUCCESS;
		}
	}

	/*
	 * If active_partition is still not selected, then neither partition
	 * is valid. Let's reinitialize the NVMEM - there is nothing else we
	 * can do.
	 */
	CPRINTS("%s: No Legacy Partitions found.", __func__);
	return EC_ERROR_INVALID_CONFIG;
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

void *nvmem_cache_base(enum nvmem_users user)
{
	if ((user < 0) || (user >= NVMEM_NUM_USERS))
		return NULL;

	return nvmem_cache + nvmem_user_start_offset[user];
}

static int nvmem_get_partition_off(int user, uint32_t offset, uint32_t len,
				   uint32_t *p_buf_offset)
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

int nvmem_init(void)
{
	int ret;

	/* Generate start offsets within partiion for user buffers */
	ret = nvmem_generate_offset_table();
	if (ret) {
		CPRINTF("%s:%d\n", __func__, __LINE__);
		return ret;
	}
	nvmem_write_error = 0;

	/*
	 * Default policy is to allow all commits. This ensures reinitialization
	 * succeeds to bootstrap the nvmem area.
	 */
	commits_enabled = 1;

	/*
	 * Try discovering legacy partition(s). If even one is present, need
	 * to migrate to the new nvmem storage scheme.
	 */
	if (nvmem_find_partition() == EC_SUCCESS)
		ret = new_nvmem_migrate(nvmem_act_partition);
	else
		ret = new_nvmem_init();

	nvmem_error_state = ret;

	if (ret != EC_SUCCESS) {
		CPRINTF("%s:%d error %d!\n", __func__, __LINE__, ret);
		return ret;
	}

	return EC_SUCCESS;
}

int nvmem_get_error_state(void) { return nvmem_error_state; }

int nvmem_is_different(uint32_t offset, uint32_t size, void *data,
		       enum nvmem_users user)
{
	int ret;
	uint32_t src_offset;

	nvmem_lock_cache();

	/* Get partition offset for this read operation */
	ret = nvmem_get_partition_off(user, offset, size, &src_offset);
	if (ret != EC_SUCCESS)
		return ret;

	/* Advance to the correct byte within the data buffer */

	/* Compare NvMem with data */
	ret = memcmp(nvmem_cache + src_offset, data, size);

	nvmem_release_cache();

	return ret;
}

int nvmem_read(uint32_t offset, uint32_t size,
		    void *data, enum nvmem_users user)
{
	int ret;
	uint32_t src_offset;

	nvmem_lock_cache();

	/* Get partition offset for this read operation */
	ret = nvmem_get_partition_off(user, offset, size, &src_offset);

	if (ret == EC_SUCCESS)
		/* Copy from src into the caller's destination buffer */
		memcpy(data, nvmem_cache + src_offset, size);

	nvmem_release_cache();

	return ret;
}

int nvmem_write(uint32_t offset, uint32_t size,
		 void *data, enum nvmem_users user)
{
	int ret;
	uint8_t *p_dest;
	uint32_t dest_offset;

	/* Make sure that the cache buffer is active */
	nvmem_lock_cache();
	nvmem_mutex.write_in_progress = 1;

	/* Compute partition offset for this write operation */
	ret = nvmem_get_partition_off(user, offset, size, &dest_offset);
	if (ret != EC_SUCCESS) {
		nvmem_write_error = 1;
		return ret;
	}

	/* Advance to correct offset within data buffer */
	p_dest = nvmem_cache + dest_offset;

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
	nvmem_lock_cache();
	nvmem_mutex.write_in_progress = 1;

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

	base_addr = (uintptr_t)nvmem_cache;
	/* Create pointer to src location within partition */
	p_src = (uint8_t *)(base_addr + s_buff_offset);
	/* Create pointer to dest location within partition */
	p_dest = (uint8_t *)(base_addr + d_buff_offset);
	/* Move the data block in NvMem */
	memmove(p_dest, p_src, size);

	return EC_SUCCESS;
}

int nvmem_enable_commits(void)
{
	if (commits_enabled)
		return EC_SUCCESS;

	if (nvmem_mutex.task != task_get_current()) {
		CPRINTF("%s: locked by task %d, attempt to unlock by task %d\n",
			__func__, nvmem_mutex.task, task_get_current());
		return EC_ERROR_INVAL;
	}

	commits_enabled = 1;
	CPRINTS("Committing NVMEM changes.");
	return nvmem_commit();
}

void nvmem_disable_commits(void)
{
	/* Will be unlocked when nvmem_enable_commits() is called. */
	nvmem_lock_cache();

	commits_enabled = 0;
}

int nvmem_commit(void)
{
	if (nvmem_mutex.task == TASK_ID_COUNT) {
		CPRINTF("%s: attempt to commit in unlocked state\n",
			__func__, nvmem_mutex.task);
		return EC_ERROR_OVERFLOW;  /* Noting to commit. */
	}

	if (nvmem_mutex.task != task_get_current()) {
		CPRINTF("%s: locked by task %d, attempt to unlock by task %d\n",
			__func__, nvmem_mutex.task, task_get_current());
		return EC_ERROR_INVAL;
	}

	/* Ensure that all writes/moves prior to commit call succeeded */
	if (nvmem_write_error) {
		CPRINTS("%s: Write Error, commit abandoned", __func__);
		/* Clear error state */
		nvmem_write_error = 0;
		commits_enabled = 1;
		nvmem_release_cache();
		return EC_ERROR_UNKNOWN;
	}

	if (!commits_enabled) {
		CPRINTS("Skipping commit");
		return EC_SUCCESS;
	}

	/* Write active partition to NvMem */
	return nvmem_save();
}

void nvmem_clear_cache(void)
{
	/* This will work only if key ladder is disabled. */
	if (DCRYPTO_ladder_is_enabled())
		return;

	nvmem_lock_cache();

	nvmem_wipe_cache();

	nvmem_save();
}
