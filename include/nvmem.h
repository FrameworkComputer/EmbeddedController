/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_NVMEM_UTILS_H
#define __CROS_EC_NVMEM_UTILS_H

/*
 * In order to provide maximum robustness for NvMem operations, the NvMem space
 * is divided into two equal sized partitions. A partition contains a tag
 * and a buffer for each NvMem user.
 *
 *     NvMem Partiion
 *     ---------------------------------------------------------------------
 *     |0x8 tag | User Buffer 0 | User Buffer 1 | .... |  User Buffer N-1  |
 *     ---------------------------------------------------------------------
 *
 *     Physical Block Tag details
 *     ---------------------------------------------------------------------
 *     |             sha               |    version      |    reserved     |
 *     ---------------------------------------------------------------------
 *         sha       -> 4 bytes of sha1 digest
 *         version   -> 1 byte version number (0 - 0xfe)
 *         reserved  -> 3 bytes
 *
 * At initialization time, each partition is scanned to see if it has a good sha
 * entry. One of the two partitions being valid is a supported condition. If
 * however, neither partiion is valid, then a check is made to see if NvMem
 * space is fully erased. If this is detected, then the tag for partion 0 is
 * populated and written into flash. If neither partition is valid and they
 * aren't fully erased, then NvMem is marked corrupt and this failure condition
 * must be reported back to the caller.
 *
 * Note that the NvMem partitions can be placed anywhere in flash space, but
 * must be equal in total size. A table is used by the NvMem module to get the
 * correct base address and offset for each partition.
 *
 * A version number is used to distinguish between two valid partitions with
 * the newsest version number (in a circular sense) marking the correct
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
 *    nvmem_compute_sha() -> function used to compute 4 byte sha (or equivalent)
 *    nvmem_wipe() -> function to erase and reformat the users' storage
 *
 * Note that total length of user buffers must satisfy the following:
 *   sum(user sizes) <= (NVMEM_PARTITION_SIZE) - sizeof(struct nvmem_tag)
 */

/* NvMem user buffer length table */
extern uint32_t nvmem_user_sizes[NVMEM_NUM_USERS];

#define NVMEM_NUM_PARTITIONS 2
#define NVMEM_SHA_SIZE 4
#define NVMEM_VERSION_BITS 8
#define NVMEM_VERSION_MASK ((1 << NVMEM_VERSION_BITS) - 1)

/* Struct for NV block tag */
struct nvmem_tag {
	uint8_t sha[NVMEM_SHA_SIZE];
	uint8_t version;
	uint8_t reserved[3];
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
 *         EC_ERROR_UNKNOWN otherwise.
 */
int nvmem_commit(void);

/**
 * One time initialization of NvMem partitions
 * @param version: Starting version number of partition 0
 *
 * @return EC_SUCCESS if flash operations are successful.
 *         EC_ERROR_UNKNOWN otherwise.
 */
int nvmem_setup(uint8_t version);

/**
 * Compute sha1 (lower 4 bytes or equivalent checksum) for NvMem tag
 *
 * @param p_buf: pointer to beginning of data
 * @param num_bytes: length of data in bytes
 * @param p_sha: pointer to where computed sha will be stored
 * @param sha_len: length in bytes to use from sha computation
 */
void nvmem_compute_sha(uint8_t *p_buf, int num_bytes, uint8_t *p_sha,
		       int sha_len);

#endif /* __CROS_EC_NVMEM_UTILS_H */
