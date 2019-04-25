/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __TPM2_NVMEM_TEST_NEW_NVMEM_H
#define __TPM2_NVMEM_TEST_NEW_NVMEM_H

#include "common.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "util.h"

#define NVMEM_NOT_INITIALIZED ((unsigned int)-1)

/*
 * A totally arbitrary byte limit for space occupied by (key, value) pairs in
 * the flash. This is an improvement compared to the legacy case where there
 * were just 272 bytes dedicated to the (key, value) pairs storage.
 */
#define MAX_VAR_TOTAL_SPACE 1000

/*
 * Let's be reasonable: we're unlikely to have keys longer than 40 or so
 * bytes, and leave full 255 bytes for the value. Total data space occupied by
 * a (key, value) pair is not to exceed the value below.
 */
#define MAX_VAR_BODY_SPACE 300

enum nn_object_type {
	NN_OBJ_OLD_COPY = 0,
	NN_OBJ_TUPLE = 1,
	NN_OBJ_TPM_RESERVED = 2,
	NN_OBJ_TPM_EVICTABLE = 3,
	NN_OBJ_TRANSACTION_DEL = 4,
	NN_OBJ_ESCAPE = 5,
	NN_OBJ_ERASED = 7,
};

/*
 * Structure placed at the base of each flash page used for NVMEM storage.
 *
 * page_number: allows to arrange pages in order they were added
 *
 * data_offset: the offset of the first element in the page (space above
 *              page header and below data_offset could be taken by the
 *              'tail' of the object stored on the previous page).
 *
 * page_hash:   is used to verify page header integrity
 */
struct nn_page_header {
	unsigned int page_number : 21;
	unsigned int data_offset : 11;
	uint32_t page_hash;
} __packed;

/*
 * Index of the 'virtual' last reserved object. RAM index space and max
 * counter objects stored at fixed location in the NVMEM cache are considered
 * reserved objects by this NVMEM flash layer.
 */
#define NV_VIRTUAL_RESERVE_LAST (NV_RESERVE_LAST + 2)

/*
 * Container header for all blobs stored in flash.
 *
 * container_type: type of object stored in the container. MAKE SURE THIS
 *                 FIELD TYPE IS THE FIRST FIELD IN THIS STRUCTURE, it is
 *                 supposed to be in the first word of the container so that
 *                 the type can be erased when object is deleted.
 *
 * container_type_copy: immutable copy of the container_type field, used to
 *                      verify contents of deleted objects.
 *
 * encrypted: set to 1 if contents are encrypted.
 *
 * size: size of the payload, 12 bits allocated, 11 bits would be enough for
 *       this use case.
 *
 * generation: a free running counter, used to compare ages of two containers
 *
 * container_hash: hash of the ENTIRE container, both header and body
 *                 included. This field is set to zero before hash is calculated
 */
struct nn_container {
	unsigned int container_type : 3;
	unsigned int container_type_copy : 3;
	unsigned int encrypted : 1;
	unsigned int size : 11;
	unsigned int generation : 2;
	unsigned int container_hash : 12;
} __packed;

/*
 * A structure to keep context of accessing to a page, page header and offset
 * define where the next access would happen.
 */
struct page_tracker {
	const struct nn_page_header *ph;
	uint16_t data_offset;
};

/*
 * Helper structure to keep track of accesses to the flash storage.
 *
 * mt:  main tracker for read or write accesses.
 *
 * ct:  keeps track of container fetches, as the location of containers has
 *      special significance: it is both part of the seed used when
 *      encrypting/decryping container contents, and also is necessary to
 *      unwind reading of the container header when the end of storage is
 *      reached and a header of all 0xff is read.
 *
 * dt:  keeps track of delimiters which is important when assessing flash
 *      contents integrity. If during startup the last item in flash is not a
 *      delimiter, this is an indication of a failed transaction, all data
 *      after the previous delimiter needs to be discarded.
 *
 * list_index; index of the current page in the list of pages, useful when
 *             sequential reading and need to get to the next page in the
 *             list.
 */

struct access_tracker {
	struct page_tracker mt; /* Main tracker. */
	struct page_tracker ct; /* Container tracker. */
	struct page_tracker dt; /* Delimiter tracker.*/
	uint8_t list_index;
};

/*
 * New nvmem interface functions, each of them could be blocking because each
 * of them acquires nvmem flash protectioin mutex before proceeding.
 */
enum ec_error_list new_nvmem_init(void);
enum ec_error_list new_nvmem_migrate(unsigned int nvmem_act_partition);
enum ec_error_list new_nvmem_save(void);
int nvmem_erase_tpm_data(void);

#if defined(TEST_BUILD) && !defined(TEST_FUZZ)
#define NVMEM_TEST_BUILD
enum ec_error_list browse_flash_contents(int);
enum ec_error_list compact_nvmem(void);
extern struct access_tracker master_at;
extern uint16_t total_var_space;
int is_uninitialized(const void *p, size_t size);
size_t init_object_offsets(uint16_t *offsets, size_t count);
struct nn_page_header *list_element_to_ph(size_t el);
void *evictable_offs_to_addr(uint16_t offset);
enum ec_error_list get_next_object(struct access_tracker *at,
				   struct nn_container *ch,
				   int include_deleted);
#endif


#endif /* ! __TPM2_NVMEM_TEST_NEW_NVMEM_H */
