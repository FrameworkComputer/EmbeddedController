/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_NVMEM_UTILS_H
#define __CROS_EC_NVMEM_UTILS_H

#include "crypto_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * In order to provide maximum robustness for NvMem operations, the NvMem space
 * is divided into two equal sized partitions. A partition contains a tag
 * and a buffer for each NvMem user.
 *
 *     NvMem Partiion
 *     ------------------------------------------------------------------------
 *     |36 byte tag | User Buffer 0 | User Buffer 1 | .... |  User Buffer N-1 |
 *     ------------------------------------------------------------------------
 *
 *     Physical Block Tag details
 *     ------------------------------------------------------------------------
 *     |      sha       |      padding     |  version  | generation | reserved |
 *     -------------------------------------------------------------------------
 *         sha        -> 16 bytes of sha1 digest
 *         padding    -> 16 bytes for future extensions
 *         version    -> nvmem layout version, currently at 0
 *         generation -> 1 byte generation number (0 - 0xfe)
 *         reserved   -> 2 bytes
 *
 * At initialization time, each partition is scanned to see if it has a good sha
 * entry. One of the two partitions being valid is a supported condition. If
 * neither partiion is valid a new partition is created with generation set to
 * zero.
 *
 * Note that the NvMem partitions can be placed anywhere in flash space, but
 * must be equal in total size. A table is used by the NvMem module to get the
 * correct base address for each partition.
 *
 * A generation number is used to distinguish between two valid partitions with
 * the newsest generation number (in a circular sense) marking the correct
 * partition to use. The parition number 0/1 is tracked via a static
 * variable. When the NvMem contents need to be updated, the flash erase/write
 * of the updated partition will use the inactive partition space in NvMem. This
 * way if there is a critical failure (i.e. loss of power) during the erase or
 * write operation, then the contents of the active partition prior the most
 * recent writes will still be preserved.
 *
 * The following CONFIG_FLASH_NVMEM_ defines are required for this module:
 *    CONFIG_FLASH_NVMEM -> enable/disable the module
 *    CONFIG_FLASH_NVMEM_OFFSET_(A|B) -> offset to start of each partition
 *    CONFIG_FLASH_NVMEM_BASE_(A|B) -> address of start of each partition
 *
 * The board.h file must define a macro or enum named NVMEM_NUM_USERS.
 * The board.c file must implement:
 *    nvmem_user_sizes[] -> array of user buffer lengths
 * The chip must provide
 *    app_compute_hash() -> function used to compute 16 byte sha (or equivalent)
 *
 * Note that total length of user buffers must satisfy the following:
 *   sum(user sizes) <= (NVMEM_PARTITION_SIZE) - sizeof(struct nvmem_tag)
 */

/* NvMem user buffer length table */
extern uint32_t nvmem_user_sizes[NVMEM_NUM_USERS];

#define NVMEM_NUM_PARTITIONS 2
#define NVMEM_SHA_SIZE CIPHER_SALT_SIZE
#define NVMEM_GENERATION_BITS 8
#define NVMEM_GENERATION_MASK (BIT(NVMEM_GENERATION_BITS) - 1)
#define NVMEM_PADDING_SIZE 16
#define NVMEM_LAYOUT_VERSION 0

/* Struct for NV block tag */
struct nvmem_tag {
	uint8_t sha[NVMEM_SHA_SIZE];
	uint8_t padding[NVMEM_PADDING_SIZE];
	uint8_t layout_version;
	uint8_t generation;
	uint8_t reserved[2];
};

/* Structure MvMem Partition */
struct nvmem_partition {
	struct nvmem_tag tag;
	uint8_t buffer[NVMEM_PARTITION_SIZE -
		       sizeof(struct nvmem_tag)];
};

/**
 * Initialize NVMem translation table and state variables
 *
 * @return EC_SUCCESS if a valid translation table is constructed, else
 *         error code.
 */
int nvmem_init(void);

/**
 * Get Nvmem internal error state
 *
 * @return nvmem_error_state variable.
 */
int nvmem_get_error_state(void);

/**
 * Compare 'size' amount of bytes in NvMem
 *
 * @param offset: Offset (in bytes) into NVmem logical space
 * @param size: Number of bytes to compare
 * @param data: Pointer to data to be compared with
 * @param user: Data section within NvMem space
 * @return 0 if the data is same, non-zero if data is different
 */
int nvmem_is_different(uint32_t offset, uint32_t size,
		       void *data, enum nvmem_users user);

/**
 * Read 'size' amount of bytes from NvMem
 *
 * @param startOffset: Offset (in bytes) into NVmem logical space
 * @param size: Number of bytes to read
 * @param data: Pointer to destination buffer
 * @param user: Data section within NvMem space
 * @return EC_ERROR_OVERFLOW (non-zero) if the read operation would exceed the
 *         buffer length of the given user, otherwise EC_SUCCESS.
 */
int nvmem_read(uint32_t startOffset, uint32_t size,
		void *data, enum nvmem_users user);

/**
 * Write 'size' amount of bytes to NvMem
 *
 * Calling this function will wait for the mutex, then lock it until
 * nvmem_commit() is invoked.
 *
 * @param startOffset: Offset (in bytes) into NVmem logical space
 * @param size: Number of bytes to write
 * @param data: Pointer to source buffer
 * @param user: Data section within NvMem space
 * @return EC_ERROR_OVERFLOW if write exceeds buffer length
 *         EC_ERROR_TIMEOUT if nvmem cache buffer is not available
 *         EC_SUCCESS if no errors.
 */
int nvmem_write(uint32_t startOffset, uint32_t size,
		 void *data, enum nvmem_users user);

/**
 * Move 'size' amount of bytes within NvMem
 *
 * Calling this function will wait for the mutex, then lock it until
 * nvmem_commit() is invoked.
 *
 * @param src_offset: source offset within NvMem logical space
 * @param dest_offset: destination offset within NvMem logical space
 * @param size: Number of bytes to move
 * @param user: Data section within NvMem space
 * @return EC_ERROR_OVERFLOW if write exceeds buffer length
 *         EC_ERROR_TIMEOUT if nvmem cache buffer is not available
 *         EC_SUCCESS if no errors.
 */
int nvmem_move(uint32_t src_offset, uint32_t dest_offset, uint32_t size,
	       enum nvmem_users user);
/**
 * Commit all previous NvMem writes to flash
 *
 * @return EC_SUCCESS if flash erase/operations are successful.

 *         EC_ERROR_OVERFLOW in case the mutex is not locked when this
 *                           function is called
 *         EC_ERROR_INVAL    if task trying to commit is not the one
 *                           holding the mutex
 *         EC_ERROR_UNKNOWN  in other error cases
 */
int nvmem_commit(void);

/*
 * Temporarily stopping NVMEM commits could be beneficial. One use case is
 * when TPM operations need to be sped up.
 *
 * Calling this function will wait for the mutex, then lock it until
 * nvmem_commit() is invoked.
 *
 * Both below functions should be called from the same task.
 */
void nvmem_disable_commits(void);

/*
 * Only the task holding the mutex is allowed to enable commits.
 *
 * @return error if this task does not hold the lock or commit
 *         fails, EC_SUCCESS otherwise.
 */
int nvmem_enable_commits(void);

/*
 * Function to retrieve the base address of the nvmem cache of the appropriate
 * user. After migration there is only one user and one base address, this
 * function will be eliminated.
 *
 * @return pointer to the base address.
 */
void *nvmem_cache_base(enum nvmem_users user);

/*
 * Clear all NVMEM cache in SRAM.
 */
void nvmem_clear_cache(void);

void nvmem_wipe_cache(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_NVMEM_UTILS_H */
