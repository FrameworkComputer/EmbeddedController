/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <string.h>

#include "test/nvmem_test.h"

#include "common.h"
#include "board.h"
#include "console.h"
#include "crypto_api.h"
#include "flash.h"
#include "flash_log.h"
#include "new_nvmem.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "shared_mem.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
/*
 * ==== Overview
 *
 * This file is the implementation of the new TPM NVMEM flash storage layer.
 * These are major differences compared to the legacy implementation:
 *
 * NVMEM TPM objects are stored in flash in separate containers, each one
 * protected by hash and possibly encrypted. When nvmem_commit() is invoked,
 * only objects changed in the NVMEM cache are updated in the flash.
 *
 * The (key, value) pairs are also stored in flash in the same kind of
 * separate containers. There is no special area allocated for the (key, value)
 * pairs in flash, they are interleaved with TPM objects.
 *
 * The (key, value) pairs are not kept in the NVMEM cache, they are stored in
 * flash only. This causes a few deviations from the legacy (key, value) pair
 * interface:
 *
 *  - no need to initialize (key, value) storage separately, initvars() API is
 *    not there.
 *
 *  - when the user is retrieving a (key, value) object, he/she is given a
 *    dynamically allocated buffer, which needs to be explicitly released by
 *    calling the new API: freevar().
 *
 *  - the (key. value) pairs, if modified, are updated in flash immediately,
 *    not after nvmem_commit() is called.
 *
 * Storing (key, value) pairs in the flash frees up 272 bytes of the cache
 * space previously used, but makes it more difficult to control how flash
 * memory is split between TPM objects and (key, value) pairs. A soft limit of
 * 1K is introduced, limiting the total space used by (key, value) pairs data,
 * not including tuple headers.
 *
 * ===== Organizing flash storage
 *
 * The total space used by the NVMEM in flash is reduced from 24 to 20
 * kilobytes, five 2K pages at the top of each flash bank. These pages are
 * concatenated into a single storage space, based on the page header placed
 * at the bottom of each page (struct nn_page_header). The page header
 * includes a 21 bit page number (this allows to order pages properly on
 * initialization), the offset of the first data object in the page and the
 * hash of the entire header.
 *
 * Yet another limitation of the new scheme is that no object stored in NVMEM
 * can exceed the flash page size less the page header size and container
 * header size. This allows for objects as large as 2035 bytes. Objects can
 * span flash pages. Note that reserved TPM object STATE_CLEAR_DATA exceeds 2K
 * in size, this is why one of its components (the .pcrSave field) is stored
 * in flash separately.
 *
 * ===== Object containers
 *
 * The container header (struct nn_container) describes the contained TPM
 * object or (key, value) pair, along with the size, and also includes the
 * hash of the entire container calculated when the hash field is set to zero.
 *
 * When an object needs to be updated, it is stored at the end of the used
 * flash space in a container with the higher .generation field value, and
 * then the older container's type field is erased, thus marking it as a
 * deleted object. The idea is that when initializing NVMEM cache after reset,
 * in case two instances of the same object are found in the flash because the
 * new instance was saved, but the old instance was not erased because of some
 * failure, the instance with larger .generation field value wins. Note that
 * this error recovery procedure is supplemented by use of transaction
 * delimiter objects described below.
 *
 * The container type field is duplicated in the container header, this allows
 * verification of the container hash after even the object was erased.
 *
 * In order to be able to erase the type the container must start at the 4
 * byte boundary. This in turn requires that each container is padded such
 * that total storage taken by the container is divisible by 4.
 *
 * To be able to tell if two containers contain two instances of the same
 * object, one needs to be able to identify the object stored in the container.
 * For the three distinct types of objects it works as follows:
 *
 *    - (key, value) pair: key, stored in the contained tuple.
 *
 *    - reserved tpm object: the first byte stored in the container. PCR
 *      values from STATE_CLEAR_DATA.pcrSave field are stored as separate
 *      reserved objects with the appropriate first bytes.
 *
 *    - evictable tpm object: the first 4 bytes stored in the container, the
 *      evictable TTPM object ID.
 *
 * Don't forget that the contents are usually encrypted. Decryption is needed
 * each time a stored object needs to be examined.
 *
 * Reserved objects of types STATE_CLEAR_DATA and STATE_RESET_DATA are very
 * big and are stored in the flash in marshaled form. On top of 'regular' TPM2
 * style marshaling, PCRs found in the STATE_CLEAR_DATA object are stored in
 * separate containers.
 *
 * ===== Storage compaction
 *
 * Keeping adding changed values at the end of the flash space would
 * inevitably cause space overflow, unless something is done about it. This is
 * where flash compaction kicks in: as soon as there are just three free flash
 * pages left the stored objects are moved to the end of the space, which
 * results in earlier used pages being freed and added to the pool of
 * available flash pages.
 *
 * A great improvement to this storage compaction process would be grouping
 * the objects such that the rarely changing ones occupy flash pages at the
 * lower page indices. In this case when compaction starts, the pages not
 * containing erased objects would not have to be re-written. This
 * optimization is left as a future enhancement.
 *
 * ===== Committing TPM changes
 *
 * When nvmem_commit() is invoked it is necessary to identify which TPM
 * objects in the cache have changed and require saving. Remember, that (key,
 * value) pairs are not held in the cache any more and are saved in the flash
 * immediately, so they do not have to be dealt with during commit.
 *
 * The commit procedure starts with iterating over the evictable objects space
 * in the NVMEM cache, storing in an array offsets of all evictable objects it
 * finds there. Then it iterates over flash contents skipping over (key,
 * value) pairs.
 *
 * For each reserved object stored in flash, it compares its stored value with
 * the value stored in the cache at known fixed location. If the value has
 * changed, a new reserved object instance is saved in flash. This approach
 * requires that all reserved objects are present in the flash, otherwise
 * there is nothing to compare the cached instance of the object with. This is
 * enforced by the init function.
 *
 * For each evictable object stored in flash, it checks if that object is
 * still in the cache using the previously collected array of offsets. If the
 * object is not in the cache, it must have been deleted by the TPM. The
 * function deletes it from the flash as well. If the object is in the cache,
 * its offset is removed from the array to indicate that the object has been
 * processed. Then if the object value has changed, the new instance is added
 * and the old instance erased. After this the only offsets left in the array
 * are offsets of new objects, not yet saved in the flash. All these remaining
 * objects get saved.
 *
 * To ensure transaction integrity, object deletions are just scheduled and
 * not processed immediately, the deletion happens after all new instances
 * have been saved in flash. See more about transaction delimiters below.
 *
 * ===== Migration from legacy storage and reclaiming flash space
 *
 * To be able to migrate existing devices from the legacy storage format the
 * initialization code checks if a full 12K flash partition is still present,
 * and if so - copies its contents into the cache and invokes the migration
 * function. The function erases the alternative partition and creates a list
 * of 5 pages available for the new format (remember, the flash footprint of
 * the new scheme is smaller, only 10K is available in each half).
 *
 * The (key, value) pairs and TPM objects are stored in the new format as
 * described, and then the legacy partition is erased and its pages are added
 * to the list of free pages. This approach would fail if the existing TPM
 * storage would exceed 10K, but this is extremely unlikely, especially since
 * the large reserved objects are stored by the new scheme in marshaled form.
 * This frees up a lot of flash space.
 *
 * Eventually it will be possible to reclaim the bottom 2K page per flash half
 * currently used by the legacy scheme, but this would be possible only after
 * migration is over. The plan is to keep a few Cr50 versions supporting the
 * migration process, and then drop the migration code and rearrange the
 * memory map and reclaim the freed pages. Chrome OS will always carry a
 * migrating capable Cr50 version along with the latest one to make sure that
 * even Chrome OS devices which had not updated their Cr50 code in a long
 * while can be migrated in two steps.
 *
 * ===== Initialization, including erased/corrupted flash
 *
 * On regular startup (no legacy partition found) the flash pages dedicated to
 * NVMEM storage are examined, pages with valid headers are included in the
 * list of available pages, sorted by the page number. Erased pages are kept
 * in a separate list. Pages which are not fully erased (but do not have a
 * valid header) are considered corrupted, are erased, and added to the second
 * list.
 *
 * After that the contents of the ordered flash pages is read, all discovered
 * TPM objects are verified and saved in the cache.
 *
 * To enforce that all reserved TPM objects are present in the flash, the init
 * routine maintains a bitmap of the reserved objects it found while
 * initializing. In the case when after the scan of the entire NVMEM flash it
 * turns out that some reserved objects have not been encountered, the init
 * routine creates new flash instances of the missing reserved objects with
 * default value of zero. This takes care of both initializing from empty
 * flash and a case when a reserved object disappears due to a bug.
 *
 * ===== Transactions support
 *
 * It is important to make sure that TPM changes are grouped together. It came
 * naturally with the legacy scheme where each time nvmem_save() was called,
 * the entire cache snapshot was saved in the flash. With the new scheme some
 * extra effort is required.
 *
 * Transaction delimiters are represented by containers of the appropriate
 * type and the payload size of zero. When nvmem_save() operation is started,
 * the new objects get written into flash and the objects requiring deletion
 * are kept in the list. Once all new objects are added to the flash, the
 * transaction delimiter is written, ending up at the top of the used flash.
 * After that the objects scheduled for deletion are deleted, and then the
 * transaction delimiter is also marked 'deleted'.
 *
 * So, during initialization the flash could be in one of three states:
 *
 * - thre is an erased transaction delimiter at the top
 *   . this is the normal state after successful commit operation.
 *
 * - there is transaction delimiter at the top, but it is not erased.
 *   . this is the case where the new objects were saved in flash, but some of
 *     the old instances might still be present not erased. The recovery
 *     procedure finds all duplicates of the objects present between two most
 *     recent delimiters and erases them.
 *
 * - there is no transaction delimiter on the top.
 *   . this is the case where nvmem_save() was interrupted before all new
 *     values have been written into the flash. The recovery procedure finds
 *     all TPM objects above the last valid delimiter in the flash and erases
 *     them all.
 *
 * ===== Handling failures
 *
 * This implementation is no better in handling failures than the legacy one,
 * it in fact is even worse, because if a failure happened during legacy
 * commit operation, at least the earlier saved partition would be available.
 * If failure happens during this implementation's save or compaction process,
 * there is a risk of ending up with a corrupted or inconsistent flash
 * contents, even though the use of transaction delimiters narrows the failure
 * window significantly.
 *
 * This first draft is offered for review and to facilitate testing and
 * discussion about how failures should be addressed.
 *
 * ===== Missing stuff
 *
 *  Presently not much thought has been given to locking and preventing
 *  problems caused by task preemption. The legacy scheme is still in place,
 *  but it might require improvements.
 */

/*
 * This code relies on the fact that space dedicated to flash NVMEM storage is
 * sufficient to guarantee that the entire NVMEM snapshot can fit into it
 * comfortably. The assert below is a very liberal computation which
 * guarantees this assumption. Note that marshaling huge reserved structures
 * reduces amount of required flash space, and this is not accounted for in
 * this calculation. Space allocated for 200 container descriptors is also way
 * more than required.
 */

/*
 * Fuzz testing does not enforce proper size definitions, causing the below
 * assert failure.
 */
BUILD_ASSERT((NEW_NVMEM_TOTAL_PAGES * CONFIG_FLASH_BANK_SIZE) >
	     (MAX_VAR_TOTAL_SPACE +
	      NV_MEMORY_SIZE +
	      200 * (sizeof(struct nn_container)) +
	      CONFIG_FLASH_BANK_SIZE * 2));

/* Maximum number of evictable objects we support. */
#define MAX_STORED_EVICTABLE_OBJECTS 20
/*
 * Container for storing (key, value) pairs, a.k.a. vars during read. Actual
 * vars would never be this large, but when looking for vars we need to be
 * able to iterate over and verify all objects in the flash, hence the max
 * body size.
 */
struct max_var_container {
	struct nn_container c_header;
	struct tuple t_header;
	uint8_t body[CONFIG_FLASH_BANK_SIZE - sizeof(struct nn_container) -
		     sizeof(struct tuple)];
} __packed;

/*
 * Limit of the number of objects which can be updated in one TPM transaction,
 * reserved and evictable total. This is much more than practical maximum.
 */
#define MAX_DELETE_CANDIDATES 30
static struct delete_candidates {
	size_t num_candidates;
	const struct nn_container *candidates[MAX_DELETE_CANDIDATES];
} *del_candidates;

/*
 * This array contains a list of flash pages indices (0..255 range) sorted by
 * the page header page number filed. Erased pages are kept at the tail of the
 * list.
 */
static uint8_t page_list[NEW_NVMEM_TOTAL_PAGES];
static uint32_t next_evict_obj_base;
static uint8_t init_in_progress;
/*
 * Mutex to protect flash space containing NVMEM objects. All operations
 * modifying the flash contents or relying on its consistency (like searching
 * in the flash) should acquire this mutex before proceeding.
 *
 * The interfaces grabbing this mutex are
 *
 *  new_nvmem_migrate()
 *  new_nvmem_init()
 *  new_nvmem_save()
 *  getvar()
 *  setvar()
 *  nvmem_erase_tpm_data()
 *
 * The only static function using the mutex is browse_flash_contents() which
 * can be invoked from the CLI and while it never modifies the flash contents,
 * it still has to be protected to be able to properly iterate over the entire
 * flash contents.
 */
static struct mutex flash_mtx;

/*
 * Wrappers around locking/unlocking mutex make it easier to debug issues by
 * adding with minimal code changes like printouts of line numbers where the
 * functions are invoked from.
 */
static void lock_mutex(int line_num)
{
	mutex_lock(&flash_mtx);
}

static void unlock_mutex(int line_num)
{
	mutex_unlock(&flash_mtx);
}

/*
 * Total space taken by key, value pairs in flash. It is limited to give TPM
 * objects priority.
 */
test_export_static uint16_t total_var_space;

/* The main context used when adding objects to NVMEM. */
test_export_static struct access_tracker master_at;

test_export_static enum ec_error_list browse_flash_contents(int print);
static enum ec_error_list save_container(struct nn_container *nc);
static void invalidate_nvmem_flash(void);

/* Log NVMEM problem as per passed in payload and size, and reboot. */
static void report_failure(struct nvmem_failure_payload *payload,
			   size_t payload_union_size)
{
	if (init_in_progress) {
		/*
		 * This must be a rolling reboot, let's invalidate flash
		 * storage to stop this.
		 */
		invalidate_nvmem_flash();
	}

	flash_log_add_event(FE_LOG_NVMEM,
			    payload_union_size +
				    offsetof(struct nvmem_failure_payload,
					     size),
			    payload);

	ccprintf("Logging failure %d, will %sreinit\n", payload->failure_type,
		 init_in_progress ? "" : "not ");

	if (init_in_progress) {
		struct nvmem_failure_payload fp;

		fp.failure_type = NVMEMF_NVMEM_WIPE;

		flash_log_add_event(
			FE_LOG_NVMEM,
			offsetof(struct nvmem_failure_payload, size), &fp);
	}

	cflush();

	system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD);
}

static void report_no_payload_failure(enum nvmem_failure_type type)
{
	struct nvmem_failure_payload fp;

	fp.failure_type = type;
	report_failure(&fp, 0);
}

/*
 * This function allocates a buffer of the requested size.
 *
 * Heap space could be very limited and at times there could be not enough
 * memory in the heap to allocate. This should not be considered a failure,
 * polling should be used instead. On a properly functioning device the memory
 * would become available. If it is not - there is not much we can do, we'll
 * have to reboot adding a log entry.
 */
static void *get_scratch_buffer(size_t size)
{
	char *buf;
	int i;
	struct nvmem_failure_payload fp;

	/*
	 * Wait up to a 5 seconds in case some other operation is under
	 * way.
	 */
	for (i = 0; i < 50; i++) {
		int rv;

		rv = shared_mem_acquire(size, &buf);
		if (rv == EC_SUCCESS) {
			if (i)
				CPRINTS("%s: waited %d cycles!", __func__, i);
			return buf;
		}
		usleep(100 * MSEC);
	}

	fp.failure_type = NVMEMF_MALLOC;
	fp.size = size;
	report_failure(&fp, sizeof(fp.size));

	/* Just to keep the compiler happy, this is never reached. */
	return NULL;
}

/* Helper function returning actual size used by NVMEM in flash. */
static size_t total_used_size(void)
{
	return master_at.list_index * CONFIG_FLASH_BANK_SIZE +
	       master_at.mt.data_offset;
}
/*
 * Helper functions to set a bit a bit at a certain index in a bitmap array
 * and to check if the bit is set. The caller must guarantee that the bitmap
 * array is large enough for the index.
 */
static int bitmap_bit_check(const uint8_t *bitmap, size_t index)
{
	return bitmap[index / 8] & (1 << (index % 8));
}

static int bitmap_bit_set(uint8_t *bitmap, size_t index)
{
	return bitmap[index / 8] |= (1 << (index % 8));
}

/* Convenience functions used to reduce amount of typecasting. */
static void app_compute_hash_wrapper(void *buf, size_t size, void *hash,
				     size_t hash_size)
{
	app_compute_hash(buf, size, hash, hash_size);
}

static STATE_CLEAR_DATA *get_scd(void)
{
	NV_RESERVED_ITEM ri;

	NvGetReserved(NV_STATE_CLEAR, &ri);

	return (STATE_CLEAR_DATA *)((uint8_t *)nvmem_cache_base(NVMEM_TPM) +
				    ri.offset);
}

/*
 * Make sure page header hash is different between prod and other types of
 * images.
 */
static uint32_t calculate_page_header_hash(struct nn_page_header *ph)
{
	uint32_t hash;
	static const uint32_t salt[] = {1, 2, 3, 4};

	BUILD_ASSERT(sizeof(hash) ==
		     offsetof(struct nn_page_header, page_hash));

	app_cipher(salt, &hash, ph, sizeof(hash));

	return hash;
}

/* Veirify page header hash. */
static int page_header_is_valid(struct nn_page_header *ph)
{
	return calculate_page_header_hash(ph) == ph->page_hash;
}

/* Convert flash page number in 0..255 range into actual flash address. */
static struct nn_page_header *flash_index_to_ph(uint8_t index)
{
	return (struct nn_page_header *)((index * CONFIG_FLASH_BANK_SIZE) +
					 CONFIG_PROGRAM_MEMORY_BASE);
}

static const void *page_cursor(const struct page_tracker *pt)
{
	return (void *)((uintptr_t)pt->ph + pt->data_offset);
}

/*
 * Return flash page pointed at by a certain page_list element if the page is
 * valid. If the index is out of range, or page is not initialized properly
 * return NULL.
 */
test_export_static struct nn_page_header *list_element_to_ph(size_t el)
{
	struct nn_page_header *ph;

	if (el >= ARRAY_SIZE(page_list))
		return NULL;

	ph = flash_index_to_ph(page_list[el]);

	if (page_header_is_valid(ph))
		return ph;

	return NULL;
}

/*
 * Read into buf or skip if buf is NULL the next num_bytes in the storage, at
 * the location determined by the passed in access tracker. Start from the
 * very beginning if the passed in access tracker is empty.
 *
 * If necessary - concatenate contents from different pages bypassing page
 * headers.
 *
 * If user is reading the container header (as specified by the
 * container_fetch argument), save in the context the location of the
 * container.
 *
 * If not enough bytes are available in the storage to satisfy the request -
 * log error and reboot.
 */
static size_t nvmem_read_bytes(struct access_tracker *at, size_t num_bytes,
			       void *buf, int container_fetch)
{
	size_t togo;
	struct nvmem_failure_payload fp;

	if (!at->list_index && !at->mt.data_offset) {
		/* Start from the beginning. */
		at->mt.ph = list_element_to_ph(0);
		at->mt.data_offset = at->mt.ph->data_offset;
	}

	if (container_fetch) {
		at->ct.data_offset = at->mt.data_offset;
		at->ct.ph = at->mt.ph;
	}

	if ((at->mt.data_offset + num_bytes) < CONFIG_FLASH_BANK_SIZE) {
		/*
		 * All requested data fits and does not even reach the top of
		 * the page.
		 */
		if (buf)
			memcpy(buf, page_cursor(&at->mt), num_bytes);

		at->mt.data_offset += num_bytes;
		return num_bytes;
	}

	/* Data is split between pages. */
	/* To go in the current page. */
	togo = CONFIG_FLASH_BANK_SIZE - at->mt.data_offset;
	if (buf) {
		memcpy(buf, page_cursor(&at->mt), togo);
		/* Next portion goes here. */
		buf = (uint8_t *)buf + togo;
	}

	/*
	 * Determine how much is there to read in the next page.
	 *
	 * Since object size is limited to page size
	 * less page header size, we are guaranteed that the object would not
	 * span more than one page boundary.
	 */
	togo = num_bytes - togo;

	/* Move to the next page. */
	at->list_index++;
	at->mt.ph = list_element_to_ph(at->list_index);

	if (!at->mt.ph && togo) {
		/*
		 * No more data to read. Could the end of used flash be close
		 * to the page boundary, so that there is no room to read an
		 * erased container header?
		 */
		if (!container_fetch) {
			fp.failure_type = NVMEMF_READ_UNDERRUN;
			fp.underrun_size = num_bytes - togo;
			/* This will never return. */
			report_failure(&fp, sizeof(fp.underrun_size));
		}

		/*
		 * Simulate reading of the container header filled with all
		 * ones, which would be an indication of the end of storage,
		 * the caller will roll back ph, data_offset and list index as
		 * appropriate.
		 */
		memset(buf, 0xff, togo);
	} else if (at->mt.ph) {
		if (at->mt.ph->data_offset < (sizeof(*at->mt.ph) + togo)) {
			fp.failure_type = NVMEMF_PH_SIZE_MISMATCH;
			fp.ph.ph_offset = at->mt.ph->data_offset;
			fp.ph.expected = sizeof(*at->mt.ph) + togo;
			/* This will never return. */
			report_failure(&fp, sizeof(fp.ph));
		}
		if (buf)
			memcpy(buf, at->mt.ph + 1, togo);

		at->mt.data_offset = sizeof(*at->mt.ph) + togo;
	}

	return num_bytes;
}

/*
 * Convert passed in absolute address into flash memory offset and write the
 * passed in blob into the flash.
 */
static enum ec_error_list write_to_flash(const void *flash_addr,
					 const void *obj, size_t size)
{
	return flash_physical_write(
		(uintptr_t)flash_addr - CONFIG_PROGRAM_MEMORY_BASE, size, obj);
}

/*
 * Corrupt headers of all active pages thus invalidating the entire NVMEM
 * flash storage.
 */
static void invalidate_nvmem_flash(void)
{
	size_t i;
	struct nn_page_header *ph;
	struct nn_page_header bad_ph;

	memset(&bad_ph, 0, sizeof(bad_ph));

	for (i = 0; i < ARRAY_SIZE(page_list); i++) {
		ph = list_element_to_ph(i);
		if (!ph)
			continue;
		write_to_flash(ph, &bad_ph, sizeof(*ph));
	}
}

/*
 * When initializing flash for the first time - set the proper first page
 * header.
 */
static enum ec_error_list set_first_page_header(void)
{
	struct nn_page_header ph = {};
	enum ec_error_list rv;
	struct nn_page_header *fph; /* Address in flash. */

	ph.data_offset = sizeof(ph);
	ph.page_hash = calculate_page_header_hash(&ph);
	fph = flash_index_to_ph(page_list[0]);
	rv = write_to_flash(fph, &ph, sizeof(ph));

	if (rv == EC_SUCCESS) {
		/* Make sure master page tracker is ready. */
		memset(&master_at, 0, sizeof(master_at));
		master_at.mt.data_offset = ph.data_offset;
		master_at.mt.ph = fph;
	}

	return rv;
}

/*
 * Verify that the passed in container is valid, specifically that its hash
 * matches its contents.
 */
static int container_is_valid(struct nn_container *ch)
{
	struct nn_container dummy_c;
	uint32_t hash;
	uint32_t preserved_hash;
	uint8_t preserved_type;

	preserved_hash = ch->container_hash;
	preserved_type = ch->container_type;

	ch->container_type = ch->container_type_copy;
	ch->container_hash = 0;
	app_compute_hash_wrapper(ch, ch->size + sizeof(*ch), &hash,
				 sizeof(hash));

	ch->container_hash = preserved_hash;
	ch->container_type = preserved_type;

	dummy_c.container_hash = hash;

	return dummy_c.container_hash == ch->container_hash;
}

static uint32_t aligned_container_size(const struct nn_container *ch)
{
	const size_t alignment_mask = CONFIG_FLASH_WRITE_SIZE - 1;

	return (ch->size + sizeof(*ch) + alignment_mask) & ~alignment_mask;
}

/*
 * Function which allows to iterate through all objects stored in flash. The
 * passed in context keeps track of where the previous object retrieval ended.
 *
 * Return:
 *  EC_SUCCESS                  if an object is retrieved and verified
 *  EC_ERROR_MEMORY_ALLOCATION  if 'erased' object reached (not an error).
 *  EC_ERROR_INVAL	        if verification failed or read is out of sync.
 */
test_export_static enum ec_error_list get_next_object(struct access_tracker *at,
						      struct nn_container *ch,
						      int include_deleted)
{
	uint32_t salt[4];
	uint8_t ctype;

	salt[3] = 0;

	do {
		size_t aligned_remaining_size;
		struct nn_container temp_ch;

		nvmem_read_bytes(at, sizeof(temp_ch), &temp_ch, 1);
		ctype = temp_ch.container_type;

		/* Should we check for the container being all 0xff? */
		if (ctype == NN_OBJ_ERASED) {
			/* Roll back container size. */
			at->mt.data_offset = at->ct.data_offset;
			at->mt.ph = at->ct.ph;

			/*
			 * If the container header happened to span between
			 * two pages or end at the page boundary - roll back
			 * page index saved in the context.
			 */
			if ((CONFIG_FLASH_BANK_SIZE - at->mt.data_offset) <=
			    sizeof(struct nn_container))
				at->list_index--;

			return EC_ERROR_MEMORY_ALLOCATION;
		}

		/*
		 * The read data is a container header, copy it into the user
		 * provided space and continue reading there.
		 */
		*ch = temp_ch;
		aligned_remaining_size =
			aligned_container_size(ch) - sizeof(*ch);

		if (aligned_remaining_size) {
			if (aligned_remaining_size >
			    (CONFIG_FLASH_BANK_SIZE - sizeof(*ch))) {
				/* Never returns. */
				report_no_payload_failure(
					NVMEMF_INCONSISTENT_FLASH_CONTENTS);
			}

			nvmem_read_bytes(at, aligned_remaining_size, ch + 1, 0);

			salt[0] = at->ct.ph->page_number;
			salt[1] = at->ct.data_offset;
			salt[2] = ch->container_hash;

			/* Decrypt in place. */
			if (!app_cipher(salt, ch + 1, ch + 1, ch->size))
				report_no_payload_failure(NVMEMF_CIPHER_ERROR);
		}

		/* And calculate hash. */
		if (!container_is_valid(ch)) {
			struct nvmem_failure_payload fp;

			if (!init_in_progress)
				report_no_payload_failure(
					NVMEMF_CONTAINER_HASH_MISMATCH);
			/*
			 * During init there might be a way to deal with
			 * this, let's just log this and continue.
			 */
			fp.failure_type = NVMEMF_CONTAINER_HASH_MISMATCH;
			flash_log_add_event(
				FE_LOG_NVMEM,
				offsetof(struct nvmem_failure_payload, size),
				&fp);

			return EC_ERROR_INVAL;
		}

		/*
		 * Keep track of the most recently encountered delimiter,
		 * finalized or not.
		 */
		if (ch->container_type_copy == NN_OBJ_TRANSACTION_DEL) {
			include_deleted = 1; /* Always return all delimiters. */

			/* But keep track only of finalized ones. */
			if (ch->container_type == NN_OBJ_OLD_COPY) {
				at->dt.ph = at->ct.ph;
				at->dt.data_offset = at->ct.data_offset;
			}
		}

	} while (!include_deleted && (ctype == NN_OBJ_OLD_COPY));

	return EC_SUCCESS;
}

/*
 * Add a delimiter object at the top of the flash. The container type field is
 * not erased.
 *
 * This is an indication that after nvmem_commit() invocation all updated
 * objects have been saved in the flash, but the old instances of the objects
 * have not yet been deleted.
 */
static enum ec_error_list add_delimiter(void)
{
	struct nn_container ch;

	memset(&ch, 0, sizeof(ch));

	ch.container_type = ch.container_type_copy = NN_OBJ_TRANSACTION_DEL;

	return save_container(&ch);
}

/*
 * Erase the container type field of the previously saved delimiter, thus
 * indicating that nvmem save transaction is completed.
 */
static enum ec_error_list finalize_delimiter(const struct nn_container *del)
{
	struct nn_container c;

	c = *del;
	c.container_type = NN_OBJ_OLD_COPY;

	return write_to_flash(del, &c, sizeof(c));
}

/* Add delimiter indicating that flash is in a consistent state. */
static enum ec_error_list add_final_delimiter(void)
{
	const struct nn_container *del;

	del = page_cursor(&master_at.mt);
	add_delimiter();

	return finalize_delimiter(del);
}

/* Erase flash page and add it to the pool of empty pages. */
static void release_flash_page(struct access_tracker *at)
{
	uint8_t page_index = page_list[0];
	void *flash;

	flash = flash_index_to_ph(page_index);
	flash_physical_erase((uintptr_t)flash - CONFIG_PROGRAM_MEMORY_BASE,
			     CONFIG_FLASH_BANK_SIZE);
	memmove(page_list, page_list + 1,
		(ARRAY_SIZE(page_list) - 1) * sizeof(page_list[0]));
	page_list[ARRAY_SIZE(page_list) - 1] = page_index;
	at->list_index--;
	master_at.list_index--;
}

/* Reshuffle flash contents dropping deleted objects. */
test_export_static enum ec_error_list compact_nvmem(void)
{
	const void *fence_ph;
	enum ec_error_list rv = EC_SUCCESS;
	size_t before;
	struct nn_container *ch;
	struct access_tracker at = {};
	int saved_object_count;
	int final_delimiter_needed = 1;

	/* How much space was used before compaction. */
	before = total_used_size();

	/* One page is enough even for the largest object. */
	ch = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE);

	/*
	 * Page where we should stop compaction, all pages before this would
	 * be recycled.
	 */
	fence_ph = master_at.mt.ph;
	saved_object_count = 0;

	do {
		switch (get_next_object(&at, ch, 0)) {
		case EC_SUCCESS:
			break;

		case EC_ERROR_MEMORY_ALLOCATION:
			shared_mem_release(ch);
			return EC_SUCCESS;

		default:
			/*
			 * The error has been reported already.
			 *
			 * This must be compaction after startup with
			 * inconsistent nvmemory state, let's make sure the
			 * top page is recycled.
			 */
			if (at.mt.ph != fence_ph)
				release_flash_page(&at);
			shared_mem_release(ch);
			return EC_ERROR_INVAL;
		}

		/* Re-store the object in compacted flash. */
		switch (ch->container_type) {
		case NN_OBJ_TUPLE:
		case NN_OBJ_TPM_RESERVED:
		case NN_OBJ_TPM_EVICTABLE:
			ch->generation++;
			if (save_container(ch) != EC_SUCCESS) {
				ccprintf("%s: Saving FAILED\n", __func__);
				shared_mem_release(ch);
				return EC_ERROR_INVAL;
			}
			saved_object_count++;
			break;
		default:
			break;
		}

		if (at.list_index != 0) {
			/*
			 * We are done with a pre-compaction page, use a
			 * delimiter to indicate that a bunch of objects are
			 * being deleted and finalize the delimiter once the
			 * old page is erased.
			 *
			 * Return the erased page to the pool of empty pages
			 * and rearrange the list of active pages.
			 */
			const void *del;

			if (saved_object_count) {
				del = page_cursor(&master_at.mt);
				add_delimiter();
			}

			release_flash_page(&at);
#if defined(NVMEM_TEST_BUILD)
			if (failure_mode == TEST_FAIL_WHEN_COMPACTING) {
				shared_mem_release(ch);
				return EC_SUCCESS;
			}
#endif

			if (saved_object_count) {
				finalize_delimiter(del);
				saved_object_count = 0;
			}
			/*
			 * No need in another delimiter if data ends on a page
			 * boundary.
			 */
			final_delimiter_needed = 0;
		} else {
			final_delimiter_needed = 1;
		}
	} while (at.mt.ph != fence_ph);

	shared_mem_release(ch);

	if (final_delimiter_needed)
		add_final_delimiter();

	CPRINTS("Compaction done, went from %d to %d bytes", before,
		total_used_size());
	return rv;
}

static void start_new_flash_page(size_t data_size)
{
	struct nn_page_header ph = {};

	ph.data_offset = sizeof(ph) + data_size;
	ph.page_number = master_at.mt.ph->page_number + 1;
	ph.page_hash = calculate_page_header_hash(&ph);
	master_at.list_index++;
	if (master_at.list_index == ARRAY_SIZE(page_list))
		report_no_payload_failure(NVMEMF_PAGE_LIST_OVERFLOW);

	master_at.mt.ph =
		(const void *)(((uintptr_t)page_list[master_at.list_index] *
				CONFIG_FLASH_BANK_SIZE) +
			       CONFIG_PROGRAM_MEMORY_BASE);

	write_to_flash(master_at.mt.ph, &ph, sizeof(ph));
	master_at.mt.data_offset = sizeof(ph);
}

/*
 * Save in the flash an object represented by the passed in container. Add new
 * pages to the list of used pages if necessary.
 */
static enum ec_error_list save_object(const struct nn_container *cont)
{
	const void *save_data = cont;
	size_t save_size = aligned_container_size(cont);
	size_t top_room;

#if defined(NVMEM_TEST_BUILD)
	if (failure_mode == TEST_FAILED_HASH)
		save_size -= sizeof(uint32_t);
#endif

	top_room = CONFIG_FLASH_BANK_SIZE - master_at.mt.data_offset;
	if (save_size >= top_room) {

		/* Let's finish the current page. */
		write_to_flash((uint8_t *)master_at.mt.ph +
				       master_at.mt.data_offset,
			       cont, top_room);

		/* Remaining data and size to be written on the next page. */
		save_data = (const void *)((uintptr_t)save_data + top_room);
		save_size -= top_room;
		start_new_flash_page(save_size);
#if defined(NVMEM_TEST_BUILD)
		if (save_size && (failure_mode == TEST_SPANNING_PAGES)) {
			ccprintf("%s:%d corrupting...\n", __func__, __LINE__);
			return EC_SUCCESS;
		}
#endif
	}

	if (save_size) {
		write_to_flash((uint8_t *)master_at.mt.ph +
				       master_at.mt.data_offset,
			       save_data, save_size);
		master_at.mt.data_offset += save_size;
	}

	return EC_SUCCESS;
}

/*
 * Functions to check if the passed in blob is all zeros or all 0xff, in both
 * cases would be considered an uninitialized value. This is used when
 * marshaling certaing structures and PCRs.
 */
static int is_all_value(const uint8_t *p, size_t size, uint8_t value)
{
	size_t i;

	for (i = 0; i < size; i++)
		if (p[i] != value)
			return 0;

	return 1;
}

test_export_static int is_uninitialized(const void *p, size_t size)
{
	return is_all_value(p, size, 0xff);
}

static int is_all_zero(const void *p, size_t size)
{
	return is_all_value(p, size, 0);
}

static int is_empty(const void *pcr_base, size_t pcr_size)
{
	return is_uninitialized(pcr_base, pcr_size) ||
	       is_all_zero(pcr_base, pcr_size);
}

/*
 * A convenience function checking if the passed in blob is not empty, and if
 * so - save the blob in the destination memory.
 *
 * Return number of bytes placed in dst or zero, if the blob was empty.
 */
static size_t copy_pcr(const uint8_t *pcr_base, size_t pcr_size, uint8_t *dst)
{
	/*
	 * We rely on the fact that all 16 PCRs of every PCR bank saved in the
	 * NVMEM's reserved space are originally set to all zeros.
	 *
	 * If all 0xFF is read - this is considered an artifact of trying to
	 * retrieve PCRs from legacy flash snapshot from the state when PCRs
	 * were not saved in the reserved space at all, i.e. also indicates an
	 * empty PCR.
	 */
	if (is_empty(pcr_base, pcr_size))
		return 0; /* No need to save this. */

	memcpy(dst, pcr_base, pcr_size);

	return pcr_size;
}

/*
 * A convenience structure and array, allowing quick access to PCR banks
 * contained in the STATE_CLEAR_DATA:pcrSave field. This helps when
 * marshailing/unmarshaling PCR contents.
 */
struct pcr_descriptor {
	uint16_t pcr_array_offset;
	uint8_t pcr_size;
} __packed;

static const struct pcr_descriptor pcr_arrays[] = {
	{offsetof(PCR_SAVE, sha1), SHA1_DIGEST_SIZE},
	{offsetof(PCR_SAVE, sha256), SHA256_DIGEST_SIZE},
	{offsetof(PCR_SAVE, sha384), SHA384_DIGEST_SIZE},
	{offsetof(PCR_SAVE, sha512), SHA512_DIGEST_SIZE}
};
#define NUM_OF_PCRS (ARRAY_SIZE(pcr_arrays) * NUM_STATIC_PCR)

/* Just in case we ever get to reducing the PCR set one way or another. */
BUILD_ASSERT(ARRAY_SIZE(pcr_arrays) == 4);
BUILD_ASSERT(NUM_OF_PCRS == 64);
/*
 * Iterate over PCRs contained in the STATE_CLEAR_DATA structure in the NVMEM
 * cache and save nonempty ones in the flash.
 */
static void migrate_pcr(STATE_CLEAR_DATA *scd, size_t array_index,
			size_t pcr_index, struct nn_container *ch)
{
	const struct pcr_descriptor *pdsc;
	uint8_t *p_container_body;
	uint8_t *pcr_base;
	uint8_t reserved_index; /* Unique ID of this PCR in reserved storage. */

	p_container_body = (uint8_t *)(ch + 1);
	pdsc = pcr_arrays + array_index;
	pcr_base = (uint8_t *)&scd->pcrSave + pdsc->pcr_array_offset +
		   pdsc->pcr_size * pcr_index;
	reserved_index = NV_VIRTUAL_RESERVE_LAST +
			 array_index * NUM_STATIC_PCR + pcr_index;

	if (!copy_pcr(pcr_base, pdsc->pcr_size, p_container_body + 1))
		return;

	p_container_body[0] = reserved_index;
	ch->size = pdsc->pcr_size + 1;
	save_container(ch);
}

/*
 * Some NVMEM structures end up in the NVMEM cache with a wrong alignment. If
 * a passed in pointer is not aligned at a 4 byte boundary, this function will
 * save the 4 bytes above the blob in the passed in space and then move the
 * blob up so that it is properly aligned.
 */
static void *preserve_struct(void *p, size_t size, uint32_t *preserved)
{
	uint32_t misalignment = ((uintptr_t)p & 3);
	void *new_p;

	if (!misalignment)
		return p; /* Nothing to adjust. */

	memcpy(preserved, (uint8_t *)p + size, sizeof(*preserved));
	new_p = (void *)((((uintptr_t)p) + 3) & ~3);
	memmove(new_p, p, size);

	return new_p;
}

static void maybe_restore_struct(void *new_p, void *old_p, size_t size,
				 uint32_t *preserved)
{
	if (!memcmp(new_p, old_p, size))
		return;

	memmove(old_p, new_p, size);
	memcpy((uint8_t *)old_p + size, preserved, sizeof(*preserved));
}

/*
 * Note that PCRs are not marshaled here, but the rest of the structre, below
 * and above the PCR array is.
 */
static uint16_t marshal_state_clear(STATE_CLEAR_DATA *scd, uint8_t *dst)
{
	PCR_AUTHVALUE *new_pav;
	STATE_CLEAR_DATA *new_scd;
	size_t bottom_size;
	size_t i;
	size_t top_size;
	uint32_t preserved;
	uint8_t *base;
	int room;

	bottom_size = offsetof(STATE_CLEAR_DATA, pcrSave);
	top_size = sizeof(scd->pcrAuthValues);

	if (is_empty(scd, bottom_size) &&
	    is_empty(&scd->pcrAuthValues, top_size) &&
	    is_empty(&scd->pcrSave.pcrCounter, sizeof(scd->pcrSave.pcrCounter)))
		return 0;

	/* Marshaling STATE_CLEAR_DATA will never need this much. */
	room = CONFIG_FLASH_BANK_SIZE;

	new_scd = preserve_struct(scd, bottom_size, &preserved);

	base = dst;

	*dst++ = (!!new_scd->shEnable) | ((!!new_scd->ehEnable) << 1) |
		 ((!!new_scd->phEnableNV) << 2);

	memcpy(dst, &new_scd->platformAlg, sizeof(new_scd->platformAlg));
	dst += sizeof(new_scd->platformAlg);

	room -= (dst - base);

	TPM2B_DIGEST_Marshal(&new_scd->platformPolicy, &dst, &room);

	TPM2B_AUTH_Marshal(&new_scd->platformAuth, &dst, &room);

	memcpy(dst, &new_scd->pcrSave.pcrCounter,
	       sizeof(new_scd->pcrSave.pcrCounter));
	dst += sizeof(new_scd->pcrSave.pcrCounter);
	room -= sizeof(new_scd->pcrSave.pcrCounter);

	maybe_restore_struct(new_scd, scd, bottom_size, &preserved);

	new_pav = preserve_struct(&scd->pcrAuthValues, top_size, &preserved);
	for (i = 0; i < ARRAY_SIZE(new_scd->pcrAuthValues.auth); i++)
		TPM2B_DIGEST_Marshal(new_pav->auth + i, &dst, &room);

	maybe_restore_struct(new_pav, &scd->pcrAuthValues, top_size,
			     &preserved);

	return dst - base;
}

static uint16_t marshal_state_reset_data(STATE_RESET_DATA *srd, uint8_t *dst)
{
	STATE_RESET_DATA *new_srd;
	uint32_t preserved;
	uint8_t *base;
	int room;

	if (is_empty(srd, sizeof(*srd)))
		return 0;

	/* Marshaling STATE_RESET_DATA will never need this much. */
	room = CONFIG_FLASH_BANK_SIZE;

	new_srd = preserve_struct(srd, sizeof(*srd), &preserved);

	base = dst;

	TPM2B_AUTH_Marshal(&new_srd->nullProof, &dst, &room);
	TPM2B_DIGEST_Marshal((TPM2B_DIGEST *)(&new_srd->nullSeed), &dst, &room);
	UINT32_Marshal(&new_srd->clearCount, &dst, &room);
	UINT64_Marshal(&new_srd->objectContextID, &dst, &room);

	memcpy(dst, new_srd->contextArray, sizeof(new_srd->contextArray));
	room -= sizeof(new_srd->contextArray);
	dst += sizeof(new_srd->contextArray);

	memcpy(dst, &new_srd->contextCounter, sizeof(new_srd->contextCounter));
	room -= sizeof(new_srd->contextCounter);
	dst += sizeof(new_srd->contextCounter);

	TPM2B_DIGEST_Marshal(&new_srd->commandAuditDigest, &dst, &room);
	UINT32_Marshal(&new_srd->restartCount, &dst, &room);
	UINT32_Marshal(&new_srd->pcrCounter, &dst, &room);

#ifdef TPM_ALG_ECC
	UINT64_Marshal(&new_srd->commitCounter, &dst, &room);
	TPM2B_NONCE_Marshal(&new_srd->commitNonce, &dst, &room);

	memcpy(dst, new_srd->commitArray, sizeof(new_srd->commitArray));
	room -= sizeof(new_srd->commitArray);
	dst += sizeof(new_srd->commitArray);
#endif

	maybe_restore_struct(new_srd, srd, sizeof(*srd), &preserved);

	return dst - base;
}

/*
 * Migrate all reserved objects found in the NVMEM cache after intializing
 * from legacy NVMEM storage.
 */
static enum ec_error_list migrate_tpm_reserved(struct nn_container *ch)
{
	STATE_CLEAR_DATA *scd = NULL;
	STATE_RESET_DATA *srd;
	size_t pcr_type_index;
	uint8_t *p_tpm_nvmem = nvmem_cache_base(NVMEM_TPM);
	uint8_t *p_container_body = (uint8_t *)(ch + 1);
	uint8_t index;

	ch->container_type = ch->container_type_copy = NN_OBJ_TPM_RESERVED;

	for (index = 0; index < NV_VIRTUAL_RESERVE_LAST; index++) {
		NV_RESERVED_ITEM ri;
		int copy_needed = 1;

		NvGetReserved(index, &ri);
		p_container_body[0] = index;

		switch (index) {
		case NV_STATE_CLEAR:
			scd = (STATE_CLEAR_DATA *)(p_tpm_nvmem + ri.offset);
			ri.size =
				marshal_state_clear(scd, p_container_body + 1);
			copy_needed = 0;
			break;

		case NV_STATE_RESET:
			srd = (STATE_RESET_DATA *)(p_tpm_nvmem + ri.offset);
			ri.size = marshal_state_reset_data(
				srd, p_container_body + 1);
			copy_needed = 0;
			break;
		}

		if (copy_needed) {
			/*
			 * Copy data into the stage area unless already done
			 * by marshaling function above.
			 */
			memcpy(p_container_body + 1, p_tpm_nvmem + ri.offset,
			       ri.size);
		}

		ch->size = ri.size + 1;
		save_container(ch);
	}

	/*
	 * Now all components but the PCRs from STATE_CLEAR_DATA have been
	 * saved, let's deal with those PCR arrays. We want to save each PCR
	 * in a separate container, as if all PCRs are extended, the total
	 * combined size of the arrays would exceed flash page size. Also,
	 * PCRs are most likely to change one or very few at a time.
	 */
	for (pcr_type_index = 0; pcr_type_index < ARRAY_SIZE(pcr_arrays);
	     pcr_type_index++) {
		size_t pcr_index;

		for (pcr_index = 0; pcr_index < NUM_STATIC_PCR; pcr_index++)
			migrate_pcr(scd, pcr_type_index, pcr_index, ch);
	}

	return EC_SUCCESS;
}

/*
 * Migrate all evictable objects found in the NVMEM cache after intializing
 * from legacy NVMEM storage.
 */
static enum ec_error_list migrate_objects(struct nn_container *ch)
{
	uint32_t next_obj_base;
	uint32_t obj_base;
	uint32_t obj_size;
	void *obj_addr;

	ch->container_type = ch->container_type_copy = NN_OBJ_TPM_EVICTABLE;

	obj_base = s_evictNvStart;
	obj_addr = nvmem_cache_base(NVMEM_TPM) + obj_base;
	memcpy(&next_obj_base, obj_addr, sizeof(next_obj_base));

	while (next_obj_base && (next_obj_base <= s_evictNvEnd)) {

		obj_size = next_obj_base - obj_base - sizeof(obj_size);
		memcpy(ch + 1, (uint32_t *)obj_addr + 1, obj_size);

		ch->size = obj_size;
		save_container(ch);

		obj_base = next_obj_base;
		obj_addr = nvmem_cache_base(NVMEM_TPM) + obj_base;

		memcpy(&next_obj_base, obj_addr, sizeof(next_obj_base));
	}

	return EC_SUCCESS;
}

static enum ec_error_list migrate_tpm_nvmem(struct nn_container *ch)
{
	/* Call this to initialize NVMEM indices. */
	NvEarlyStageFindHandle(0);

	migrate_tpm_reserved(ch);
	migrate_objects(ch);

	return EC_SUCCESS;
}

static enum ec_error_list save_var(const uint8_t *key, uint8_t key_len,
				   const uint8_t *val, uint8_t val_len,
				   struct max_var_container *vc)
{
	const int total_size =
		key_len + val_len + offsetof(struct max_var_container, body);
	enum ec_error_list rv;
	int local_alloc = !vc;

	if (local_alloc) {
		vc = get_scratch_buffer(total_size);
		vc->c_header.generation = 0;
	}

	/* Fill up tuple body. */
	vc->t_header.key_len = key_len;
	vc->t_header.val_len = val_len;
	memcpy(vc->body, key, key_len);
	memcpy(vc->body + key_len, val, val_len);

	/* Set up container header. */
	vc->c_header.container_type_copy = vc->c_header.container_type =
		NN_OBJ_TUPLE;
	vc->c_header.encrypted = 1;
	vc->c_header.size = sizeof(struct tuple) + val_len + key_len;

	rv = save_container(&vc->c_header);
	if (rv == EC_SUCCESS)
		total_var_space += key_len + val_len;

	if (local_alloc)
		shared_mem_release(vc);

	return rv;
}

/*
 * Migrate all (key, value) pairs found in the NVMEM cache after intializing
 * from legacy NVMEM storage.
 */
static enum ec_error_list migrate_vars(struct nn_container *ch)
{
	const struct tuple *var;

	/*
	 * During migration (key, value) pairs need to be manually copied from
	 * the NVMEM cache.
	 */
	set_local_copy();
	var = NULL;
	total_var_space = 0;

	while ((var = legacy_getnextvar(var)) != NULL)
		save_var(var->data_, var->key_len, var->data_ + var->key_len,
			 var->val_len, (struct max_var_container *)ch);

	return EC_SUCCESS;
}

static int erase_partition(unsigned int act_partition, int erase_backup)
{
	enum ec_error_list rv;
	size_t flash_base;

	/*
	 * This is the first time we save using the new scheme, let's prepare
	 * the flash space. First determine which half is the backup now and
	 * erase it.
	 */
	flash_base = (act_partition ^ erase_backup) ? CONFIG_FLASH_NVMEM_BASE_A
						    : CONFIG_FLASH_NVMEM_BASE_B;
	flash_base -= CONFIG_PROGRAM_MEMORY_BASE;

	rv = flash_physical_erase(flash_base, NVMEM_PARTITION_SIZE);

	if (rv != EC_SUCCESS) {
		ccprintf("%s: flash erase failed\n", __func__);
		return -rv;
	}

	return flash_base + CONFIG_FLASH_BANK_SIZE;
}

/*
 * This function is called once in a lifetime, when Cr50 boots up and a legacy
 * partition if found in the flash.
 */
enum ec_error_list new_nvmem_migrate(unsigned int act_partition)
{
	int flash_base;
	int i;
	int j;
	struct nn_container *ch;

	if (!crypto_enabled())
		return EC_ERROR_INVAL;

	/*
	 * This is the first time we save using the new scheme, let's prepare
	 * the flash space. First determine which half is the backup now and
	 * erase it.
	 */
	flash_base = erase_partition(act_partition, 1);
	if (flash_base < 0) {
		ccprintf("%s: backup partition erase failed\n", __func__);
		return -flash_base;
	}

	ch = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE);

	lock_mutex(__LINE__);

	/* Populate half of page_list with available page offsets. */
	for (i = 0; i < ARRAY_SIZE(page_list) / 2; i++)
		page_list[i] = flash_base / CONFIG_FLASH_BANK_SIZE + i;

	set_first_page_header();

	ch->encrypted = 1;
	ch->generation = 0;

	migrate_vars(ch);
	migrate_tpm_nvmem(ch);

	shared_mem_release(ch);

	add_final_delimiter();

	unlock_mutex(__LINE__);

	if (browse_flash_contents(0) != EC_SUCCESS)
		/* Never returns. */
		report_no_payload_failure(NVMEMF_MIGRATION_FAILURE);

	CPRINTS("Migration success, used %d bytes of flash",
		total_used_size());

	/*
	 * Now we can erase the active partition and add its flash to the pool.
	 */
	flash_base = erase_partition(act_partition, 0);
	if (flash_base < 0)
		/* Never returns. */
		report_no_payload_failure(NVMEMF_LEGACY_ERASE_FAILURE);

	/*
	 * Populate the second half of the page_list with pages retrieved from
	 * legacy partition.
	 */
	for (j = 0; j < ARRAY_SIZE(page_list) / 2; j++)
		page_list[i + j] = flash_base / CONFIG_FLASH_BANK_SIZE + j;

	return EC_SUCCESS;
}

/* Check if the passed in flash page is empty, if not - erase it. */
static void verify_empty_page(void *ph)
{
	uint32_t *word_p = ph;
	size_t i;

	for (i = 0; i < (CONFIG_FLASH_BANK_SIZE / sizeof(*word_p)); i++) {
		if (word_p[i] != (uint32_t)~0) {
			CPRINTS("%s: corrupted page at %p!", __func__, word_p);
			flash_physical_erase((uintptr_t)word_p -
						     CONFIG_PROGRAM_MEMORY_BASE,
					     CONFIG_FLASH_BANK_SIZE);
			break;
		}
	}
}

/*
 * At startup initialize the list of pages which contain NVMEM data and erased
 * pages. The list (in fact an array containing indices of the pages) is
 * sorted by the page number found in the page header. Pages which do not
 * contain valid page header are checked to be erased and are placed at the
 * tail of the list.
 */
static void init_page_list(void)
{
	size_t i;
	size_t j;
	size_t page_list_index = 0;
	size_t tail_index;
	struct nn_page_header *ph;

	tail_index = ARRAY_SIZE(page_list);

	for (i = 0; i < ARRAY_SIZE(page_list); i++) {
		uint32_t page_index;

		/*
		 * This will yield indices of top pages first, first from the
		 * bottom half of the flash, and then from the top half. We
		 * know that flash is 512K in size, and pages are 2K in size,
		 * the indices will be in 123..127 and 251..255 range.
		 */
		if (i < (ARRAY_SIZE(page_list) / 2)) {
			page_index = (CONFIG_FLASH_NEW_NVMEM_BASE_A -
				      CONFIG_PROGRAM_MEMORY_BASE) /
					     CONFIG_FLASH_BANK_SIZE +
				     i;
		} else {
			page_index = (CONFIG_FLASH_NEW_NVMEM_BASE_B -
				      CONFIG_PROGRAM_MEMORY_BASE) /
					     CONFIG_FLASH_BANK_SIZE -
				     ARRAY_SIZE(page_list) / 2 + i;
		}

		ph = flash_index_to_ph(page_index);

		if (!page_header_is_valid(ph)) {
			/*
			 * this is not a valid page, let's plug it in into the
			 * tail of the list.
			 */
			page_list[--tail_index] = page_index;
			verify_empty_page(ph);
			continue;
		}

		/* This seems a valid page, let's put it in order. */
		for (j = 0; j < page_list_index; j++) {
			struct nn_page_header *prev_ph;

			prev_ph = list_element_to_ph(j);

			if (prev_ph->page_number > ph->page_number) {
				/* Need to move up. */
				memmove(page_list + j + 1, page_list + j,
					sizeof(page_list[0]) *
						(page_list_index - j));
				break;
			}
		}

		page_list[j] = page_index;
		page_list_index++;
	}

	if (!page_list_index) {
		CPRINTS("Init nvmem from scratch");
		set_first_page_header();
		page_list_index++;
	}
}

/*
 * The passed in pointer contains marshaled STATE_CLEAR structure as retrieved
 * from flash. This function unmarshals it and places in the NVMEM cache where
 * it belongs. Note that PCRs were not marshaled.
 */
static void unmarshal_state_clear(uint8_t *pad, int size, uint32_t offset)
{
	STATE_CLEAR_DATA *real_scd;
	STATE_CLEAR_DATA *scd;
	size_t i;
	uint32_t preserved;
	uint8_t booleans;

	real_scd = (STATE_CLEAR_DATA *)((uint8_t *)nvmem_cache_base(NVMEM_TPM) +
					offset);

	memset(real_scd, 0, sizeof(*real_scd));
	if (!size)
		return;

	memcpy(&preserved, real_scd + 1, sizeof(preserved));

	scd = (void *)(((uintptr_t)real_scd + 3) & ~3);

	/* Need proper unmarshal. */
	booleans = *pad++;
	scd->shEnable = !!(booleans & 1);
	scd->ehEnable = !!(booleans & (1 << 1));
	scd->phEnableNV = !!(booleans & (1 << 2));
	size--;

	memcpy(&scd->platformAlg, pad, sizeof(scd->platformAlg));
	pad += sizeof(scd->platformAlg);
	size -= sizeof(scd->platformAlg);

	TPM2B_DIGEST_Unmarshal(&scd->platformPolicy, &pad, &size);
	TPM2B_AUTH_Unmarshal(&scd->platformAuth, &pad, &size);

	memcpy(&scd->pcrSave.pcrCounter, pad, sizeof(scd->pcrSave.pcrCounter));
	pad += sizeof(scd->pcrSave.pcrCounter);
	size -= sizeof(scd->pcrSave.pcrCounter);

	for (i = 0; i < ARRAY_SIZE(scd->pcrAuthValues.auth); i++)
		TPM2B_DIGEST_Unmarshal(scd->pcrAuthValues.auth + i, &pad,
				       &size);

	memmove(real_scd, scd, sizeof(*scd));
	memcpy(real_scd + 1, &preserved, sizeof(preserved));
}

/*
 * The passed in pointer contains marshaled STATE_RESET structure as retrieved
 * from flash. This function unmarshals it and places in the NVMEM cache where
 * it belongs.
 */
static void unmarshal_state_reset(uint8_t *pad, int size, uint32_t offset)
{
	STATE_RESET_DATA *real_srd;
	STATE_RESET_DATA *srd;
	uint32_t preserved;

	real_srd = (STATE_RESET_DATA *)((uint8_t *)nvmem_cache_base(NVMEM_TPM) +
					offset);

	memset(real_srd, 0, sizeof(*real_srd));
	if (!size)
		return;

	memcpy(&preserved, real_srd + 1, sizeof(preserved));

	srd = (void *)(((uintptr_t)real_srd + 3) & ~3);

	TPM2B_AUTH_Unmarshal(&srd->nullProof, &pad, &size);
	TPM2B_DIGEST_Unmarshal((TPM2B_DIGEST *)(&srd->nullSeed), &pad, &size);
	UINT32_Unmarshal(&srd->clearCount, &pad, &size);
	UINT64_Marshal(&srd->objectContextID, &pad, &size);

	memcpy(srd->contextArray, pad, sizeof(srd->contextArray));
	size -= sizeof(srd->contextArray);
	pad += sizeof(srd->contextArray);

	memcpy(&srd->contextCounter, pad, sizeof(srd->contextCounter));
	size -= sizeof(srd->contextCounter);
	pad += sizeof(srd->contextCounter);

	TPM2B_DIGEST_Unmarshal(&srd->commandAuditDigest, &pad, &size);
	UINT32_Unmarshal(&srd->restartCount, &pad, &size);
	UINT32_Unmarshal(&srd->pcrCounter, &pad, &size);

#ifdef TPM_ALG_ECC
	UINT64_Unmarshal(&srd->commitCounter, &pad, &size);
	TPM2B_NONCE_Unmarshal(&srd->commitNonce, &pad, &size);

	memcpy(srd->commitArray, pad, sizeof(srd->commitArray));
	size -= sizeof(srd->commitArray);
#endif

	memmove(real_srd, srd, sizeof(*srd));
	memcpy(real_srd + 1, &preserved, sizeof(preserved));
}

/*
 * Based on the passed in index, find the location of the PCR in the NVMEM
 * cache and copy it there.
 */
static void restore_pcr(size_t pcr_index, uint8_t *pad, size_t size)
{
	const STATE_CLEAR_DATA *scd;
	const struct pcr_descriptor *pcrd;
	void *cached; /* This PCR's position in the NVMEM cache. */

	if (pcr_index > NUM_OF_PCRS)
		return; /* This is an error. */

	pcrd = pcr_arrays + pcr_index / NUM_STATIC_PCR;
	if (pcrd->pcr_size != size)
		return; /* This is an error. */

	scd = get_scd();
	cached = (uint8_t *)&scd->pcrSave + pcrd->pcr_array_offset +
		 pcrd->pcr_size * (pcr_index % NUM_STATIC_PCR);

	memcpy(cached, pad, size);
}

/* Restore a reserved object found in flash on initialization. */
static void restore_reserved(void *pad, size_t size, uint8_t *bitmap)
{
	NV_RESERVED_ITEM ri;
	uint16_t type;
	void *cached;

	/*
	 * Index is saved as a single byte, update pad to point at the
	 * payload.
	 */
	type = *(uint8_t *)pad++;
	size--;

	if (type < NV_VIRTUAL_RESERVE_LAST) {
		NvGetReserved(type, &ri);

		bitmap_bit_set(bitmap, type);

		switch (type) {
		case NV_STATE_CLEAR:
			unmarshal_state_clear(pad, size, ri.offset);
			break;

		case NV_STATE_RESET:
			unmarshal_state_reset(pad, size, ri.offset);
			break;

		default:
			cached = ((uint8_t *)nvmem_cache_base(NVMEM_TPM) +
				  ri.offset);
			memcpy(cached, pad, size);
			break;
		}
		return;
	}

	restore_pcr(type - NV_VIRTUAL_RESERVE_LAST, pad, size);
}

/* Restore an evictable object found in flash on initialization. */
static void restore_object(void *pad, size_t size)
{
	uint8_t *dest;

	if (!next_evict_obj_base)
		next_evict_obj_base = s_evictNvStart;

	dest = ((uint8_t *)nvmem_cache_base(NVMEM_TPM) + next_evict_obj_base);
	next_evict_obj_base += size + sizeof(next_evict_obj_base);
	memcpy(dest, &next_evict_obj_base, sizeof(next_evict_obj_base));

	dest += sizeof(next_evict_obj_base);
	memcpy(dest, pad, size);
	dest += size;

	memset(dest, 0, sizeof(next_evict_obj_base));
}

/*
 * When starting from scratch (flash fully erased) there would be no reserved
 * objects in NVMEM, and for the commit to work properly, every single
 * reserved object needs to be present in the flash so that its value is
 * compared with the cache contents.
 *
 * There is also an off chance of a bug where a reserved value is lost in the
 * flash - it would never be reinstated even after TPM reinitializes.
 *
 * The reserved_bitmap array is a bitmap of all detected reserved objects,
 * those not in the array are initialized to a dummy initial value.
 */
static enum ec_error_list verify_reserved(uint8_t *reserved_bitmap,
					  struct nn_container *ch)
{
	enum ec_error_list rv;
	int i;
	uint8_t *container_body;
	int delimiter_needed = 0;

	/* All uninitted reserved objects set to zero. */
	memset(ch, 0, CONFIG_FLASH_BANK_SIZE);

	ch->container_type = ch->container_type_copy = NN_OBJ_TPM_RESERVED;
	ch->encrypted = 1;
	container_body = (uint8_t *)(ch + 1);

	rv = EC_SUCCESS;

	for (i = 0; i < NV_VIRTUAL_RESERVE_LAST; i++) {
		NV_RESERVED_ITEM ri;

		if (bitmap_bit_check(reserved_bitmap, i))
			continue;

		NvGetReserved(i, &ri);
		container_body[0] = i;

		switch (i) {
			/*
			 * No need to save these on initialization from
			 * scratch, unmarshaling code will properly expand
			 * size of zero.
			 */
		case NV_STATE_CLEAR:
		case NV_STATE_RESET:
			ri.size = 0;
			break;

			/*
			 * This is used for Ram Index field, prepended by
			 * size. Set the size to minimum, the size of the size
			 * field.
			 */
		case NV_RAM_INDEX_SPACE:
			ri.size = sizeof(uint32_t);
			break;

		default:
			break;
		}

		delimiter_needed = 1;

		ch->size = ri.size + 1;
		rv = save_container(ch);

		/* Clean up encrypted contents. */
		memset(container_body + 1, 0, ri.size);

		if (rv != EC_SUCCESS)
			break;
	}

	if (delimiter_needed && (rv == EC_SUCCESS))
		add_final_delimiter();

	return rv;
}

static enum ec_error_list invalidate_object(const struct nn_container *ch)
{
	struct nn_container c_copy;

	c_copy = *ch;
	c_copy.container_type = NN_OBJ_OLD_COPY;

	return write_to_flash(ch, &c_copy, sizeof(uint32_t));
}

static enum ec_error_list delete_object(const struct access_tracker *at,
					struct nn_container *ch)
{
	const void *flash_ch;

	flash_ch = page_cursor(&at->ct);

	if (memcmp(ch, flash_ch, sizeof(uint32_t)))
		report_no_payload_failure(NVMEMF_PRE_ERASE_MISMATCH);

	if (!del_candidates)
		return invalidate_object(flash_ch);

	/*
	 * Do not delete the object yet, save it in the list of delete
	 * candidates.
	 */
	if (del_candidates->num_candidates ==
	    ARRAY_SIZE(del_candidates->candidates))
		report_no_payload_failure(NVMEMF_EXCESS_DELETE_OBJECTS);

	del_candidates->candidates[del_candidates->num_candidates++] = flash_ch;
	return EC_SUCCESS;
}

static enum ec_error_list verify_last_section(
	const struct page_tracker *prev_del, struct nn_container *ch)
{
	/*
	 * This is very inefficient, but we do this only when recovering from
	 * botched nvmem saves.
	 *
	 * For each object found between prev_del and last_del we need to
	 * check if there are earlier instances of these objects in the flash
	 * which are not yet deleted, and delete them if found.
	 */
	struct object {
		uint8_t cont_type;
		union {
			uint32_t handle; /* For evictables. */
			uint8_t id;	 /* For reserved objects. */
			struct {	 /* For tuples. */
				uint32_t key_hash;
				uint8_t key_len;
			};
		};
	};
	struct new_objects {
		uint8_t num_objects;
		struct object objects[2 * MAX_DELETE_CANDIDATES];
	};

	struct access_tracker at;
	struct new_objects *newobjs;
	struct object *po;
	uint8_t ctype;
	struct page_tracker top_del;
	struct max_var_container *vc;
	int i;

	newobjs = get_scratch_buffer(sizeof(struct new_objects));

	at.mt = *prev_del;
	for (i = 0; i < ARRAY_SIZE(page_list); i++)
		if (list_element_to_ph(i) == at.mt.ph) {
			at.list_index = i;
			break;
		}

	po = newobjs->objects;

	while (get_next_object(&at, ch, 0) == EC_SUCCESS) {
		ctype = ch->container_type;

		/* Speculative assignment, might be unused. */
		po->cont_type = ctype;
		switch (ctype) {
		case NN_OBJ_TPM_RESERVED:
			po->id = *((uint8_t *)(ch + 1));
			break;

		case NN_OBJ_TPM_EVICTABLE:
			po->handle = *((uint32_t *)(ch + 1));
			break;

		case NN_OBJ_TUPLE:
			vc = (struct max_var_container *)ch;
			po->key_len = vc->t_header.key_len;
			app_compute_hash_wrapper(vc->t_header.data_,
						 po->key_len, &po->key_hash,
						 sizeof(po->key_hash));
			break;
		default:
			continue;
		}
		if (++(newobjs->num_objects) == ARRAY_SIZE(newobjs->objects))
			/* Never returns. */
			report_no_payload_failure(NVMEMF_SECTION_VERIFY);
		po++;
	}

	/*
	 * Last object read from flash should have been a non-finalized
	 * delimiter.
	 */
	if (ch->container_type != NN_OBJ_TRANSACTION_DEL) {
		struct nvmem_failure_payload fp;

		fp.failure_type = NVMEMF_UNEXPECTED_LAST_OBJ;
		fp.last_obj_type = ch->container_type;
		/* Never returns. */
		report_failure(&fp, sizeof(fp.last_obj_type));
	}

	/*
	 * Now we have a cache of of objects which were updated but their old
	 * instances could have been left in the flash. Let's iterate over the
	 * flash and delete those if found.
	 */
	memset(&at, 0, sizeof(at));
	while ((at.mt.ph != prev_del->ph) &&
	       (at.mt.data_offset != prev_del->data_offset)) {
		size_t i;
		size_t key_size;
		uint32_t key;

		if (get_next_object(&at, ch, 0) != EC_SUCCESS)
			report_no_payload_failure(NVMEMF_MISSING_OBJECT);

		ctype = ch->container_type;

		switch (ctype) {
		case NN_OBJ_TPM_RESERVED:
			key = *((uint8_t *)(ch + 1));
			key_size = sizeof(uint8_t);
			break;

		case NN_OBJ_TPM_EVICTABLE:
			key = *((uint32_t *)(ch + 1));
			key_size = sizeof(uint32_t);
			break;

		case NN_OBJ_TUPLE:
			vc = (struct max_var_container *)ch;
			key_size = vc->t_header.key_len;
			app_compute_hash_wrapper(vc->t_header.data_, key_size,
						 &key, sizeof(key));
			break;

		default:
			continue;
		}

		for (i = 0, po = newobjs->objects; i < newobjs->num_objects;
		     i++, po++) {
			if (po->cont_type != ctype)
				continue;

			if ((ctype == NN_OBJ_TPM_RESERVED) && (po->id != key))
				continue;

			if ((ctype == NN_OBJ_TPM_EVICTABLE) &&
			    (po->handle != key))
				continue;

			if ((ctype == NN_OBJ_TUPLE) &&
			    ((po->key_len != key_size) ||
			     (key != po->key_hash)))
				continue;

			/*
			 * This indeed is a leftover which needs to be
			 * deleted.
			 */
			delete_object(&at, ch);
		}
	}
	shared_mem_release(newobjs);
	if (master_at.mt.data_offset > sizeof(struct nn_page_header)) {
		top_del.ph = master_at.mt.ph;
		top_del.data_offset =
			master_at.mt.data_offset - sizeof(struct nn_container);
	} else {
		top_del.ph = list_element_to_ph(master_at.list_index - 1);
		top_del.data_offset =
			CONFIG_FLASH_BANK_SIZE - -sizeof(struct nn_container);
	}

	return finalize_delimiter(page_cursor(&top_del));
}

/*
 * This function is called during initialization after the entire flash
 * contents were scanned, to verify that flash is in a valid state.
 */
static enum ec_error_list verify_delimiter(struct nn_container *nc)
{
	enum ec_error_list rv;
	/* Used to read starting at last good delimiter. */
	struct access_tracker dpt = {};

	if ((master_at.list_index == 0) &&
	    (master_at.mt.data_offset == sizeof(struct nn_page_header))) {
		/* This must be an init from scratch, no delimiter yet. */
		if (!master_at.dt.ph)
			return EC_SUCCESS;

		/* This is bad, will have to wipe out everything. */
		return EC_ERROR_INVAL;
	}

	if (nc->container_type_copy == NN_OBJ_TRANSACTION_DEL) {
		if (nc->container_type == NN_OBJ_OLD_COPY)
			return EC_SUCCESS;
		/*
		 * The delimiter is there, but it has not been finalized,
		 * which means that there might be objects in the flash which
		 * were not updated after the last delimiter was written.
		 */
		return verify_last_section(&master_at.dt, nc);
	}

	/*
	 * The delimiter is not there, everything above the last verified
	 * delimiter must go.
	 *
	 * First, create a context for retrieving objects starting at the last
	 * valid delimiter, make sure list index is set properly.
	 */
	dpt.mt = master_at.dt;
	if (dpt.mt.ph == master_at.mt.ph) {
		dpt.list_index = master_at.list_index;
	} else {
		uint8_t i;

		for (i = 0; i < master_at.list_index; i++)
			if (list_element_to_ph(i) == dpt.mt.ph) {
				dpt.list_index = i;
				break;
			}
	}

	while ((rv = get_next_object(&dpt, nc, 0)) == EC_SUCCESS)
		delete_object(&dpt, nc);

	if (rv == EC_ERROR_INVAL) {
		/*
		 * There must have been an interruption of the saving process,
		 * let's wipe out flash to the end of the current page and
		 * compact the storage.
		 */
		size_t remainder_size;
		const void *p = page_cursor(&master_at.ct);

		if (dpt.ct.ph != dpt.mt.ph) {
			/*
			 * The last retrieved object is spanning flash page
			 * boundary.
			 *
			 * If this is not the last object in the flash, this
			 * is an unrecoverable init failure.
			 */
			if ((dpt.mt.ph != master_at.mt.ph) ||
			    (list_element_to_ph(dpt.list_index - 1) !=
			     dpt.ct.ph))
				report_no_payload_failure(
					NVMEMF_CORRUPTED_INIT);
			/*
			 * Let's erase the page where the last object spilled
			 * into.
			 */
			flash_physical_erase((uintptr_t)dpt.mt.ph -
						     CONFIG_PROGRAM_MEMORY_BASE,
					     CONFIG_FLASH_BANK_SIZE);
			/*
			 * And move it to the available pages part of the
			 * pages list.
			 */
			master_at.list_index -= 1;
			master_at.mt = dpt.ct;
		}

		remainder_size = CONFIG_FLASH_BANK_SIZE - dpt.ct.data_offset;
		memset(nc, 0, remainder_size);
		write_to_flash(p, nc, remainder_size);
		/* Make sure compaction starts with the new page. */
		start_new_flash_page(0);
		compact_nvmem();
	} else {
		/* Add delimiter at the very top. */
		add_final_delimiter();
	}

	/* Need to re-read the NVMEM cache. */
	return EC_ERROR_TRY_AGAIN;
}

/*
 * At startup iterate over flash contents and move TPM objects into the
 * appropriate locations in the NVMEM cache.
 */
static enum ec_error_list retrieve_nvmem_contents(void)
{
	int rv;
	int tries;
	struct max_var_container *vc;
	struct nn_container *nc;
	uint8_t res_bitmap[(NV_PSEUDO_RESERVE_LAST + 7) / 8];

	/* No saved object will exceed CONFIG_FLASH_BANK_SIZE in size. */
	nc = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE);

	/*
	 * Depending on the state of flash, we might have to do this three
	 * times.
	 */
	for (tries = 0; tries < 3; tries++) {
		memset(&master_at, 0, sizeof(master_at));
		memset(nvmem_cache_base(NVMEM_TPM), 0,
		       nvmem_user_sizes[NVMEM_TPM]);
		memset(res_bitmap, 0, sizeof(res_bitmap));
		next_evict_obj_base = 0;

		while ((rv = get_next_object(&master_at, nc, 0)) ==
		       EC_SUCCESS) {
			switch (nc->container_type) {
			case NN_OBJ_TUPLE:
				vc = (struct max_var_container *)nc;
				total_var_space += vc->t_header.key_len +
						   vc->t_header.val_len;
				break; /* Keep tuples in flash. */
			case NN_OBJ_TPM_RESERVED:
				restore_reserved(nc + 1, nc->size, res_bitmap);
				break;

			case NN_OBJ_TPM_EVICTABLE:
				restore_object(nc + 1, nc->size);
				break;
			default:
				break;
			}
		}

		rv = verify_delimiter(nc);

		if (rv != EC_ERROR_TRY_AGAIN)
			break;
	}

	if (rv != EC_SUCCESS)
		report_no_payload_failure(NVMEMF_UNRECOVERABLE_INIT);

	rv = verify_reserved(res_bitmap, nc);

	shared_mem_release(nc);

	return rv;
}

enum ec_error_list new_nvmem_init(void)
{
	enum ec_error_list rv;
	timestamp_t start, init;

	if (!crypto_enabled())
		return EC_ERROR_INVAL;

	init_in_progress = 1;

	total_var_space = 0;

	/* Initialize NVMEM indices. */
	NvEarlyStageFindHandle(0);

	lock_mutex(__LINE__);

	init_page_list();

	start = get_time();

	rv = retrieve_nvmem_contents();

	init = get_time();

	unlock_mutex(__LINE__);

	init_in_progress = 0;

	CPRINTS("init took %d", (uint32_t)(init.val - start.val));

	return rv;
}

/*
 * Browse through the flash storage and save all evictable objects' offsets in
 * the passed in array. This is used to keep track of objects added or deleted
 * by the TPM library.
 */
test_export_static size_t init_object_offsets(uint16_t *offsets, size_t count)
{
	size_t num_objects = 0;
	uint32_t next_obj_base;
	uint32_t obj_base;
	void *obj_addr;

	obj_base = s_evictNvStart;
	obj_addr = (uint8_t *)nvmem_cache_base(NVMEM_TPM) + obj_base;
	memcpy(&next_obj_base, obj_addr, sizeof(next_obj_base));

	while (next_obj_base && (next_obj_base <= s_evictNvEnd)) {
		if (num_objects == count) {
			/* What do we do here?! */
			ccprintf("Too many objects!\n");
			break;
		}

		offsets[num_objects++] =
			obj_base - s_evictNvStart + sizeof(next_obj_base);

		obj_addr = nvmem_cache_base(NVMEM_TPM) + next_obj_base;
		obj_base = next_obj_base;
		memcpy(&next_obj_base, obj_addr, sizeof(next_obj_base));
	}

	return num_objects;
}

static enum ec_error_list update_object(const struct access_tracker *at,
					struct nn_container *ch,
					void *cached_object, size_t new_size)
{
	size_t copy_size = new_size;
	size_t preserved_size;
	uint32_t preserved_hash;
	uint8_t *dst = (uint8_t *)(ch + 1);

	preserved_size = ch->size;
	preserved_hash = ch->container_hash;

	/*
	 * Need to copy data into the container, skip reserved type if it is a
	 * reserved object.
	 */
	if (ch->container_type == NN_OBJ_TPM_RESERVED) {
		dst++;
		copy_size--;
	}
	memcpy(dst, cached_object, copy_size);

	ch->generation++;
	ch->size = new_size;
	save_container(ch);

	ch->generation--;
	ch->size = preserved_size;
	ch->container_hash = preserved_hash;
	return delete_object(at, ch);
}

static enum ec_error_list update_pcr(const struct access_tracker *at,
				     struct nn_container *ch, uint8_t index,
				     uint8_t *cached)
{
	uint8_t preserved;

	cached--;
	preserved = cached[0];
	cached[0] = index;
	update_object(at, ch, cached, ch->size);
	cached[0] = preserved;

	return EC_SUCCESS;
}

static enum ec_error_list save_pcr(struct nn_container *ch,
				   uint8_t reserved_index, const void *pcr,
				   size_t pcr_size)
{
	uint8_t *container_body;

	ch->container_type = ch->container_type_copy = NN_OBJ_TPM_RESERVED;
	ch->encrypted = 1;
	ch->size = pcr_size + 1;
	ch->generation = 0;

	container_body = (uint8_t *)(ch + 1);
	container_body[0] = reserved_index;
	memcpy(container_body + 1, pcr, pcr_size);

	return save_container(ch);
}

static enum ec_error_list maybe_save_pcr(struct nn_container *ch,
					 size_t pcr_index)
{
	const STATE_CLEAR_DATA *scd;
	const struct pcr_descriptor *pcrd;
	const void *cached;
	size_t pcr_size;

	pcrd = pcr_arrays + pcr_index / NUM_STATIC_PCR;
	scd = get_scd();

	pcr_size = pcrd->pcr_size;

	cached = (const uint8_t *)&scd->pcrSave + pcrd->pcr_array_offset +
		 pcr_size * (pcr_index % NUM_STATIC_PCR);

	if (is_empty(cached, pcr_size))
		return EC_SUCCESS;

	return save_pcr(ch, pcr_index + NV_VIRTUAL_RESERVE_LAST, cached,
			pcr_size);
}

/*
 * The process_XXX functions below are used to check and if necessary add,
 * update or delete objects from the flash based on the NVMEM cache
 * contents.
 */
static enum ec_error_list process_pcr(const struct access_tracker *at,
				      struct nn_container *ch, uint8_t index,
				      const uint8_t *saved, uint8_t *pcr_bitmap)
{
	STATE_CLEAR_DATA *scd;
	const struct pcr_descriptor *pcrd;
	size_t pcr_bitmap_index;
	size_t pcr_index;
	size_t pcr_size;
	uint8_t *cached;

	pcr_bitmap_index = index - NV_VIRTUAL_RESERVE_LAST;

	if (pcr_bitmap_index > NUM_OF_PCRS)
		return EC_ERROR_INVAL;

	pcrd = pcr_arrays + pcr_bitmap_index / NUM_STATIC_PCR;
	pcr_index = pcr_bitmap_index % NUM_STATIC_PCR;

	pcr_size = pcrd->pcr_size;

	if (pcr_size != (ch->size - 1))
		return EC_ERROR_INVAL; /* This is an error. */

	/* Find out base address of the cached PCR. */
	scd = get_scd();
	cached = (uint8_t *)&scd->pcrSave + pcrd->pcr_array_offset +
		 pcr_size * pcr_index;

	/* Set bitmap bit to indicate that this PCR was looked at. */
	bitmap_bit_set(pcr_bitmap, pcr_bitmap_index);

	if (memcmp(saved, cached, pcr_size))
		return update_pcr(at, ch, index, cached);

	return EC_SUCCESS;
}

static enum ec_error_list process_reserved(const struct access_tracker *at,
					   struct nn_container *ch,
					   uint8_t *pcr_bitmap)
{
	NV_RESERVED_ITEM ri;
	size_t new_size;
	uint8_t *saved;
	uint8_t index;
	void *cached;

	/*
	 * Find out this object's location in the cache (first byte of the
	 * contents is the index of the reserved object.
	 */
	saved = (uint8_t *)(ch + 1);
	index = *saved++;

	NvGetReserved(index, &ri);

	if (ri.size) {
		void *marshaled;

		cached = (uint8_t *)nvmem_cache_base(NVMEM_TPM) + ri.offset;

		/*
		 * For NV_STATE_CLEAR and NV_STATE_RESET cases Let's marshal
		 * cached data to be able to compare it with saved data.
		 */
		if (index == NV_STATE_CLEAR) {
			marshaled = ((uint8_t *)(ch + 1)) + ch->size;
			new_size = marshal_state_clear(cached, marshaled);
			cached = marshaled;
		} else if (index == NV_STATE_RESET) {
			marshaled = ((uint8_t *)(ch + 1)) + ch->size;
			new_size = marshal_state_reset_data(cached, marshaled);
			cached = marshaled;
		} else {
			new_size = ri.size;
		}

		if ((new_size == (ch->size - 1)) &&
		    !memcmp(saved, cached, new_size))
			return EC_SUCCESS;

		return update_object(at, ch, cached, new_size + 1);
	}

	/* This must be a PCR. */
	return process_pcr(at, ch, index, saved, pcr_bitmap);
}

static enum ec_error_list process_object(const struct access_tracker *at,
					 struct nn_container *ch,
					 uint16_t *tpm_object_offsets,
					 size_t *num_objects)
{
	size_t i;
	uint32_t cached_size;
	uint32_t cached_type;
	uint32_t flash_type;
	uint32_t next_obj_base;
	uint8_t *evict_start;
	void *pcache;

	evict_start = (uint8_t *)nvmem_cache_base(NVMEM_TPM) + s_evictNvStart;
	memcpy(&flash_type, ch + 1, sizeof(flash_type));
	for (i = 0; i < *num_objects; i++) {

		/* Find TPM object in the NVMEM cache. */
		pcache = evict_start + tpm_object_offsets[i];
		memcpy(&cached_type, pcache, sizeof(cached_type));
		if (cached_type == flash_type)
			break;
	}

	if (i == *num_objects) {
		/*
		 * This object is not in the cache any more, delete it from
		 * flash.
		 */
		return delete_object(at, ch);
	}

	memcpy(&next_obj_base, (uint8_t *)pcache - sizeof(next_obj_base),
	       sizeof(next_obj_base));
	cached_size = next_obj_base - s_evictNvStart - tpm_object_offsets[i];
	if ((cached_size != ch->size) || memcmp(ch + 1, pcache, cached_size)) {
		/*
		 * Object changed. Let's delete the old copy and save the new
		 * one.
		 */
		update_object(at, ch, pcache, ch->size);
	}

	tpm_object_offsets[i] = tpm_object_offsets[*num_objects - 1];
	*num_objects -= 1;

	return EC_SUCCESS;
}

static enum ec_error_list save_new_object(uint16_t obj_base, void *buf)
{
	size_t obj_size;
	struct nn_container *ch = buf;
	uint32_t next_obj_base;
	void *obj_addr;

	obj_addr = (uint8_t *)nvmem_cache_base(NVMEM_TPM) + obj_base +
		   s_evictNvStart;
	memcpy(&next_obj_base, obj_addr - sizeof(next_obj_base),
	       sizeof(next_obj_base));
	obj_size = next_obj_base - obj_base - s_evictNvStart;

	ch->container_type_copy = ch->container_type = NN_OBJ_TPM_EVICTABLE;
	ch->encrypted = 1;
	ch->size = obj_size;
	ch->generation = 0;
	memcpy(ch + 1, obj_addr, obj_size);

	return save_container(ch);
}

static enum ec_error_list new_nvmem_save_(void)
{
	const void *fence_ph;
	size_t i;
	size_t num_objs;
	struct nn_container *ch;
	struct access_tracker at = {};
	uint16_t fence_offset;
	/* We don't foresee ever storing this many objects. */
	uint16_t tpm_object_offsets[MAX_STORED_EVICTABLE_OBJECTS];
	uint8_t pcr_bitmap[(NUM_STATIC_PCR * ARRAY_SIZE(pcr_arrays) + 7) / 8];

	/* See if compaction is needed. */
	if (master_at.list_index >= (ARRAY_SIZE(page_list) - 3)) {
		enum ec_error_list rv;

		rv = compact_nvmem();
		if (rv != EC_SUCCESS)
			return rv;
	}

	fence_ph = master_at.mt.ph;
	fence_offset = master_at.mt.data_offset;

	num_objs = init_object_offsets(tpm_object_offsets,
				       ARRAY_SIZE(tpm_object_offsets));

	memset(pcr_bitmap, 0, sizeof(pcr_bitmap));
	del_candidates = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE +
					    sizeof(struct delete_candidates));
	ch = (void *)(del_candidates + 1);
	del_candidates->num_candidates = 0;

	while ((fence_ph != at.mt.ph) || (fence_offset != at.mt.data_offset)) {
		int rv;

		rv = get_next_object(&at, ch, 0);

		if (rv == EC_ERROR_MEMORY_ALLOCATION)
			break;

		if (rv != EC_SUCCESS) {
			ccprintf("%s: failed to read flash when saving (%d)!\n",
				 __func__, rv);
			shared_mem_release(ch);
			return rv;
		}

		if (ch->container_type == NN_OBJ_TPM_RESERVED) {
			process_reserved(&at, ch, pcr_bitmap);
			continue;
		}

		if (ch->container_type == NN_OBJ_TPM_EVICTABLE) {
			process_object(&at, ch, tpm_object_offsets, &num_objs);
			continue;
		}
	}

	/* Now save new objects, if any. */
	for (i = 0; i < num_objs; i++)
		save_new_object(tpm_object_offsets[i], ch);

	/* And new pcrs, if any. */
	for (i = 0; i < NUM_OF_PCRS; i++) {
		if (bitmap_bit_check(pcr_bitmap, i))
			continue;
		maybe_save_pcr(ch, i);
	}

#if defined(NVMEM_TEST_BUILD)
	if (failure_mode == TEST_FAIL_WHEN_SAVING) {
		shared_mem_release(del_candidates);
		del_candidates = NULL;
		return EC_SUCCESS;
	}
#endif
	/*
	 * Add a delimiter if there have been new containers added to the
	 * flash.
	 */
	if (del_candidates->num_candidates ||
	    (fence_offset != master_at.mt.data_offset) ||
	    (fence_ph != master_at.mt.ph)) {
		const void *del = page_cursor(&master_at.mt);

		add_delimiter();

		if (del_candidates->num_candidates) {
			/* Now delete objects which need to be deleted. */
			for (i = 0; i < del_candidates->num_candidates; i++)
				invalidate_object(
					del_candidates->candidates[i]);
		}

#if defined(NVMEM_TEST_BUILD)
		if (failure_mode == TEST_FAIL_WHEN_INVALIDATING) {
			shared_mem_release(del_candidates);
			del_candidates = NULL;
			return EC_SUCCESS;
		}
#endif
		finalize_delimiter(del);
	}

	shared_mem_release(del_candidates);
	del_candidates = NULL;

	return EC_SUCCESS;
}

enum ec_error_list new_nvmem_save(void)
{
	enum ec_error_list rv;

	if (!crypto_enabled())
		return EC_ERROR_INVAL;

	lock_mutex(__LINE__);
	rv = new_nvmem_save_();
	unlock_mutex(__LINE__);

	return rv;
}

/* Caller must free memory allocated by this function! */
static struct max_var_container *find_var(const uint8_t *key, size_t key_len,
					  struct access_tracker *at)
{
	int rv;
	struct max_var_container *vc;

	vc = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE);

	/*
	 * Let's iterate over all objects there are and look for matching
	 * tuples.
	 */
	while ((rv = get_next_object(at, &vc->c_header, 0)) == EC_SUCCESS) {

		if (vc->c_header.container_type != NN_OBJ_TUPLE)
			continue;

		/* Verify consistency, first that the sizes match */
		if ((vc->t_header.key_len + vc->t_header.val_len +
		     sizeof(vc->t_header)) != vc->c_header.size) {
			ccprintf("%s: - inconsistent sizes!\n", __func__);
			/* report error here. */
			continue;
		}

		/* Ok, found a tuple, does the key match? */
		if ((key_len == vc->t_header.key_len) &&
		    !memcmp(key, vc->body, key_len))
			/* Yes, it does! */
			return vc;
	}

	shared_mem_release(vc);
	return NULL;
}

const struct tuple *getvar(const uint8_t *key, uint8_t key_len)
{
	const struct max_var_container *vc;
	struct access_tracker at = {};

	if (!crypto_enabled())
		return NULL;

	if (!key || !key_len)
		return NULL;

	lock_mutex(__LINE__);
	vc = find_var(key, key_len, &at);
	unlock_mutex(__LINE__);

	if (vc)
		return &vc->t_header;

	return NULL;
}

void freevar(const struct tuple *var)
{
	void *vc;

	if (!var)
		return;

	vc = (uint8_t *)var - offsetof(struct max_var_container, t_header);
	shared_mem_release(vc);
}

static enum ec_error_list save_container(struct nn_container *nc)
{
	uint32_t hash;
	uint32_t salt[4];

	nc->container_hash = 0;
	app_compute_hash_wrapper(nc, sizeof(*nc) + nc->size, &hash,
				 sizeof(hash));
	nc->container_hash = hash; /* This will truncate it. */

	/* Skip transactions delimiters. */
	if (nc->size) {
		salt[0] = master_at.mt.ph->page_number;
		salt[1] = master_at.mt.data_offset;
		salt[2] = nc->container_hash;
		salt[3] = 0;

		if (!app_cipher(salt, nc + 1, nc + 1, nc->size))
			report_no_payload_failure(NVMEMF_CIPHER_ERROR);
	}

	return save_object(nc);
}

static int setvar_(const uint8_t *key, uint8_t key_len, const uint8_t *val,
		   uint8_t val_len)
{
	enum ec_error_list rv;
	int erase_request;
	size_t new_var_space;
	size_t old_var_space;
	struct max_var_container *vc;
	struct access_tracker at = {};
	const struct nn_container *del;

	if (!key || !key_len)
		return EC_ERROR_INVAL;

	new_var_space = key_len + val_len;

	if (new_var_space > MAX_VAR_BODY_SPACE)
		/* Too much space would be needed. */
		return EC_ERROR_INVAL;

	erase_request = !val || !val_len;

	/* See if compaction is needed. */
	if (!erase_request &&
	    (master_at.list_index >= (ARRAY_SIZE(page_list) - 3))) {
		rv = compact_nvmem();
		if (rv != EC_SUCCESS)
			return rv;
	}

	vc = find_var(key, key_len, &at);

	if (erase_request) {
		if (!vc)
			/* Nothing to erase. */
			return EC_SUCCESS;

		rv = invalidate_object(
			(struct nn_container *)((uintptr_t)at.ct.ph +
						at.ct.data_offset));

		if (rv == EC_SUCCESS)
			total_var_space -=
				vc->t_header.key_len + vc->t_header.val_len;

		shared_mem_release(vc);
		return rv;
	}

	/* Is this variable already there? */
	if (!vc) {
		/* No, it is not. Will it fit? */
		if ((new_var_space + total_var_space) > MAX_VAR_TOTAL_SPACE)
			/* No, it will not. */
			return EC_ERROR_OVERFLOW;

		rv = save_var(key, key_len, val, val_len, vc);
		if (rv == EC_SUCCESS)
			add_final_delimiter();

		return rv;
	}

	/* The variable was found, let's see if the value is being changed. */
	if (vc->t_header.val_len == val_len &&
	    !memcmp(val, vc->body + key_len, val_len)) {
		shared_mem_release(vc);
		return EC_SUCCESS;
	}

	/* Ok, the variable was found, and is of a different value. */
	old_var_space = vc->t_header.val_len + vc->t_header.key_len;

	if ((old_var_space < new_var_space) &&
	    ((total_var_space + new_var_space - old_var_space) >
	     MAX_VAR_TOTAL_SPACE)) {
		shared_mem_release(vc);
		return EC_ERROR_OVERFLOW;
	}

	/* Save the new instance first with the larger generation number. */
	vc->c_header.generation++;
	rv = save_var(key, key_len, val, val_len, vc);
	shared_mem_release(vc);
	del = page_cursor(&master_at.mt);
#if defined(NVMEM_TEST_BUILD)
	if (failure_mode == TEST_FAIL_SAVING_VAR)
		return EC_SUCCESS;
#endif
	add_delimiter();
	if (rv == EC_SUCCESS) {
		rv = invalidate_object(
			(struct nn_container *)((uintptr_t)at.ct.ph +
						at.ct.data_offset));
		if (rv == EC_SUCCESS) {
			total_var_space -= old_var_space;
#if defined(NVMEM_TEST_BUILD)
			if (failure_mode != TEST_FAIL_FINALIZING_VAR)
#endif
				finalize_delimiter(del);
		}
	}
	return rv;
}

int setvar(const uint8_t *key, uint8_t key_len, const uint8_t *val,
	   uint8_t val_len)
{
	int rv;

	if (!crypto_enabled())
		return EC_ERROR_INVAL;

	lock_mutex(__LINE__);
	rv = setvar_(key, key_len, val, val_len);
	unlock_mutex(__LINE__);

	return rv;
}

static void dump_contents(const struct nn_container *ch)
{
	const uint8_t *buf = (const void *)ch;
	size_t i;
	size_t total_size = sizeof(*ch) + ch->size;

	for (i = 0; i < total_size; i++) {
		if (!(i % 16)) {
			ccprintf("\n");
			cflush();
		}
		ccprintf(" %02x", buf[i]);
	}
	ccprintf("\n");
}

/*
 * Clear tpm data from nvmem. First fill up the current top page with erased
 * objects, then compact the flash storage, removing all TPM related objects.
 * This would guarantee that all pages where TPM objecs were stored would be
 * erased.
 */
int nvmem_erase_tpm_data(void)
{
	const uint8_t *key;
	const uint8_t *val;
	int rv;
	struct nn_container *ch;
	struct access_tracker at = {};
	uint8_t saved_list_index;
	uint8_t key_len;

	if (!crypto_enabled())
		return EC_ERROR_INVAL;

	ch = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE);

	lock_mutex(__LINE__);

	while (get_next_object(&at, ch, 0) == EC_SUCCESS) {

		if ((ch->container_type != NN_OBJ_TPM_RESERVED) &&
		    (ch->container_type != NN_OBJ_TPM_EVICTABLE))
			continue;

		delete_object(&at, ch);
	}

	unlock_mutex(__LINE__);

	shared_mem_release(ch);

	/*
	 * Now fill up the current flash page with erased objects to make sure
	 * that it would be erased during next compaction. Use dummy key,
	 * value pairs as the erase objects.
	 */
	saved_list_index = master_at.list_index;
	key = (const uint8_t *)nvmem_erase_tpm_data;
	val = (const uint8_t *)nvmem_erase_tpm_data;
	key_len = MAX_VAR_BODY_SPACE - 255;
	do {
		size_t to_go_in_page;
		uint8_t val_len;

		to_go_in_page =
			CONFIG_FLASH_BANK_SIZE - master_at.mt.data_offset;
		if (to_go_in_page >
		    (MAX_VAR_BODY_SPACE +
		     offsetof(struct max_var_container, body) - 1)) {
			val_len = MAX_VAR_BODY_SPACE - key_len;
		} else {
			/*
			 * Let's not write more than we have to get over the
			 * page limit. The minimum size we need is:
			 *
			 * <container header size> + <tuple header size> + 2
			 *
			 * (where key and value are of one byte each).
			 */
			if (to_go_in_page <
			    (offsetof(struct max_var_container, body) + 2)) {
				/*
				 * There is very little room left, even key
				 * and value of size of one each is enough to
				 * go over.
				 */
				key_len = 1;
				val_len = 1;
			} else {
				size_t need_to_cover;

				/* How much space key and value should cover? */
				need_to_cover =
					to_go_in_page -
					offsetof(struct max_var_container,
						 body) + 1;
				key_len = need_to_cover / 2;
				val_len = need_to_cover - key_len;
			}
		}
		if (setvar(key, key_len, val, val_len) != EC_SUCCESS)
			ccprintf("%s: adding var failed!\n", __func__);
		if (setvar(key, key_len, NULL, 0) != EC_SUCCESS)
			ccprintf("%s: deleting var failed!\n", __func__);

	} while (master_at.list_index != (saved_list_index + 1));

	lock_mutex(__LINE__);
	rv = compact_nvmem();
	unlock_mutex(__LINE__);

	if (rv == EC_SUCCESS)
		rv = new_nvmem_init();

	return rv;
}

/*
 * Function which verifes flash contents integrity (and printing objects it
 * finds, if requested by the caller). All objects' active and deleted alike
 * integrity is verified by get_next_object().
 */
test_export_static enum ec_error_list browse_flash_contents(int print)
{
	int active = 0;
	int count = 0;
	int rv = EC_SUCCESS;
	size_t line_len = 0;
	struct nn_container *ch;
	struct access_tracker at = {};

	if (!crypto_enabled()) {
		ccprintf("Crypto services not available\n");
		return EC_ERROR_INVAL;
	}

	ch = get_scratch_buffer(CONFIG_FLASH_BANK_SIZE);
	lock_mutex(__LINE__);

	while ((rv = get_next_object(&at, ch, 1)) == EC_SUCCESS) {
		uint8_t ctype = ch->container_type;

		count++;

		if ((ctype != NN_OBJ_OLD_COPY) &&
		    (ctype != NN_OBJ_TRANSACTION_DEL))
			active++;

		if (print) {
			char erased;

			if (ctype == NN_OBJ_OLD_COPY)
				erased = 'x';
			else
				erased = ' ';

			if (ch->container_type_copy == NN_OBJ_TPM_RESERVED) {
				ccprintf("%cR:%02x.%d       ", erased,
					 *((uint8_t *)(ch + 1)),
					 ch->generation);
			} else {
				uint32_t index;
				char tag;

				switch (ch->container_type_copy) {
				case NN_OBJ_TPM_EVICTABLE:
					tag = 'E';
					break;

				case NN_OBJ_TUPLE:
					tag = 'T';
					break;

				case NN_OBJ_TRANSACTION_DEL:
					tag = 's'; /* 's' for separator. */
					break;

				default:
					tag = '?';
					break;
				}

				if (ch->container_type_copy !=
				    NN_OBJ_TRANSACTION_DEL)
					memcpy(&index, ch + 1, sizeof(index));
				else
					index = 0;
				ccprintf("%c%c:%08x.%d ", erased, tag, index,
					 ch->generation);
			}
			if (print > 1) {
				dump_contents(ch);
				continue;
			}

			if (line_len > 70) {
				ccprintf("\n");
				cflush();
				line_len = 0;
			} else {
				line_len += 11;
			}
		}
	}

	unlock_mutex(__LINE__);

	shared_mem_release(ch);

	if (rv == EC_ERROR_MEMORY_ALLOCATION) {
		ccprintf("%schecked %d objects, %d active\n", print ? "\n" : "",
			 count, active);
		rv = EC_SUCCESS;
	}

	return rv;
}

static int command_dump_nvmem(int argc, char **argv)
{
	int print = 1;

	nvmem_disable_commits();

#ifdef CR50_DEV
	/* Allow dumping ecnrypted NVMEM contents only to DEV builds. */
	print += (argc > 1);
#endif
	browse_flash_contents(print);

	nvmem_enable_commits();

	return 0;
}
DECLARE_SAFE_CONSOLE_COMMAND(dump_nvmem, command_dump_nvmem, "", "");
