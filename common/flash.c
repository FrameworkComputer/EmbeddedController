/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC - common functions */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "builtin/assert.h"
#ifdef CONFIG_ZEPHYR
#include "cbi_flash.h"
#endif /* CONFIG_ZEPHYR */
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "otp.h"
#include "rwsig.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"
#include "vboot_hash.h"
#include "write_protect.h"

/*
 * Contents of erased flash, as a 32-bit value.  Most platforms erase flash
 * bits to 1.
 */
#ifndef CONFIG_FLASH_ERASED_VALUE32
#define CONFIG_FLASH_ERASED_VALUE32 (-1U)
#endif

#ifdef CONFIG_FLASH_PSTATE

/*
 * If flash isn't mapped to the EC's address space, it's probably SPI, and
 * should be using SPI write protect, not PSTATE.
 */
#if !defined(CONFIG_INTERNAL_STORAGE) || !defined(CONFIG_MAPPED_STORAGE)
#error "PSTATE should only be used with internal mem-mapped flash."
#endif

#ifdef CONFIG_FLASH_PSTATE_BANK
/* Persistent protection state - emulates a SPI status register for flashrom */
/* NOTE: It's not expected that RO and RW will support
 * differing PSTATE versions. */
#define PERSIST_STATE_VERSION 3 /* Expected persist_state.version */

/* Flags for persist_state.flags */
/* Protect persist state and RO firmware at boot */
#define PERSIST_FLAG_PROTECT_RO 0x02
#define PSTATE_VALID_FLAGS BIT(0)
#define PSTATE_VALID_SERIALNO BIT(1)
#define PSTATE_VALID_MAC_ADDR BIT(2)

/*
 * Error correction code operates on blocks equal to CONFIG_FLASH_WRITE_SIZE
 * bytes so the persist_state must be a multiple of that length. To ensure this
 * occurs, the aligned attribute has been set. Alignment has a side effect
 * in that pointer arithmetic can't break alignment so it adds padding to the
 * size of the structure to ensure that it is also a multiple of the alignment.
 */
struct persist_state {
	uint8_t version; /* Version of this struct */
	uint8_t flags; /* Lock flags (PERSIST_FLAG_*) */
	uint8_t valid_fields; /* Flags for valid data. */
	uint8_t reserved; /* Reserved; set 0 */
#ifdef CONFIG_SERIALNO_LEN
	uint8_t serialno[CONFIG_SERIALNO_LEN]; /* Serial number. */
#endif /* CONFIG_SERIALNO_LEN */
#ifdef CONFIG_MAC_ADDR_LEN
	uint8_t mac_addr[CONFIG_MAC_ADDR_LEN];
#endif /* CONFIG_MAC_ADDR_LEN */
} __aligned(CONFIG_FLASH_WRITE_SIZE);

/* written with flash_physical_write, need to respect alignment constraints */
BUILD_ASSERT(sizeof(struct persist_state) % CONFIG_FLASH_WRITE_SIZE == 0);

BUILD_ASSERT(sizeof(struct persist_state) <= CONFIG_FW_PSTATE_SIZE);

#else /* !CONFIG_FLASH_PSTATE_BANK */

/*
 * Flags for write protect state depend on the erased value of flash.  The
 * locked value must be the same as the unlocked value with one or more bits
 * transitioned away from the erased state.  That way, it is possible to
 * rewrite the data in-place to set the lock.
 *
 * STM32F0x can only write 0x0000 to a non-erased half-word, which means
 * PSTATE_MAGIC_LOCKED isn't quite as pretty.  That's ok; the only thing
 * we actually need to detect is PSTATE_MAGIC_UNLOCKED, since that's the
 * only value we'll ever alter, and the only value which causes us not to
 * lock the flash at boot.
 */
#if (CONFIG_FLASH_ERASED_VALUE32 == -1U)
#define PSTATE_MAGIC_UNLOCKED 0x4f4e5057 /* "WPNO" */
#define PSTATE_MAGIC_LOCKED 0x00000000 /* ""     */
#elif (CONFIG_FLASH_ERASED_VALUE32 == 0)
#define PSTATE_MAGIC_UNLOCKED 0x4f4e5057 /* "WPNO" */
#define PSTATE_MAGIC_LOCKED 0x5f5f5057 /* "WP__" */
#else
/* What kind of wacky flash doesn't erase all bits to 1 or 0? */
#error "PSTATE needs magic values for this flash architecture."
#endif

/*
 * Rewriting the write protect flag in place currently requires a minimum write
 * size <= the size of the flag value.
 *
 * We could work around this on chips with larger minimum write size by reading
 * the write block containing the flag into RAM, changing it to the locked
 * value, and then rewriting that block.  But we should only pay for that
 * complexity when we run across another chip which needs it.
 */
#if (CONFIG_FLASH_WRITE_SIZE > 4)
#error "Non-bank-based PSTATE requires flash write size <= 32 bits."
#endif

const uint32_t pstate_data __attribute__((section(".rodata.pstate"))) =
#ifdef CONFIG_FLASH_PSTATE_LOCKED
	PSTATE_MAGIC_LOCKED;
#else
	PSTATE_MAGIC_UNLOCKED;
#endif

#endif /* !CONFIG_FLASH_PSTATE_BANK */
#endif /* CONFIG_FLASH_PSTATE */

/* Shim layer provides implementation of these functions based on Zephyr API */
#if !defined(CONFIG_ZEPHYR) || \
	!defined(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT)
#ifdef CONFIG_FLASH_MULTIPLE_REGION
const struct ec_flash_bank *flash_bank_info(int bank)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(flash_bank_array); i++) {
		if (bank < flash_bank_array[i].count)
			return &flash_bank_array[i];
		bank -= flash_bank_array[i].count;
	}

	return NULL;
}

int crec_flash_bank_size(int bank)
{
	int rv;
	const struct ec_flash_bank *info = flash_bank_info(bank);

	if (!info)
		return -1;

	rv = BIT(info->size_exp);
	ASSERT(rv > 0);
	return rv;
}

int crec_flash_bank_erase_size(int bank)
{
	int rv;
	const struct ec_flash_bank *info = flash_bank_info(bank);

	if (!info)
		return -1;

	rv = BIT(info->erase_size_exp);
	ASSERT(rv > 0);
	return rv;
}

int crec_flash_bank_index(int offset)
{
	int bank_offset = 0, i;

	if (offset == 0)
		return bank_offset;

	for (i = 0; i < ARRAY_SIZE(flash_bank_array); i++) {
		int all_sector_size = flash_bank_array[i].count
				      << flash_bank_array[i].size_exp;
		if (offset >= all_sector_size) {
			offset -= all_sector_size;
			bank_offset += flash_bank_array[i].count;
			continue;
		}
		if (offset & ((1 << flash_bank_array[i].size_exp) - 1))
			return -1;
		return bank_offset + (offset >> flash_bank_array[i].size_exp);
	}
	if (offset != 0)
		return -1;
	return bank_offset;
}

int crec_flash_bank_count(int offset, int size)
{
	int begin = crec_flash_bank_index(offset);
	int end = crec_flash_bank_index(offset + size);

	if (begin == -1 || end == -1)
		return -1;
	return end - begin;
}

int crec_flash_bank_start_offset(int bank)
{
	int i;
	int offset;
	int bank_size;

	if (bank < 0)
		return -1;

	offset = 0;
	for (i = 0; i < bank; i++) {
		bank_size = crec_flash_bank_size(i);
		if (bank_size < 0)
			return -1;
		offset += bank_size;
	}

	return offset;
}

int crec_flash_response_fill_banks(struct ec_response_flash_info_2 *r,
				   int num_banks)
{
	const struct ec_flash_bank *banks = flash_bank_array;
	int banks_to_copy = MIN(ARRAY_SIZE(flash_bank_array), num_banks);

	r->num_banks_desc = banks_to_copy;
	r->num_banks_total = ARRAY_SIZE(flash_bank_array);
	if (banks_to_copy > 0)
		memcpy(r->banks, banks,
		       banks_to_copy * sizeof(struct ec_flash_bank));

	return EC_RES_SUCCESS;
}
#else /* CONFIG_FLASH_MULTIPLE_REGION */
#if CONFIG_FLASH_BANK_SIZE < CONFIG_FLASH_ERASE_SIZE
#error "Flash: Bank size expected bigger or equal to erase size."
#endif
int crec_flash_response_fill_banks(struct ec_response_flash_info_2 *r,
				   int num_banks)
{
	if (num_banks >= 1) {
		r->banks[0].count = crec_flash_total_banks();
		r->banks[0].size_exp = __fls(CONFIG_FLASH_BANK_SIZE);
		r->banks[0].write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE);
		r->banks[0].erase_size_exp = __fls(CONFIG_FLASH_ERASE_SIZE);
		r->banks[0].protect_size_exp = __fls(CONFIG_FLASH_BANK_SIZE);

		r->num_banks_desc = 1;
	} else {
		/* num_banks == 0, don't fill the banks array */
		r->num_banks_desc = 0;
	}

	r->num_banks_total = 1;

	return EC_RES_SUCCESS;
}
#endif /* CONFIG_FLASH_MULTIPLE_REGION */

int crec_flash_total_banks(void)
{
	return PHYSICAL_BANKS;
}
#endif /* !defined(CONFIG_ZEPHYR) ||                                \
	* !defined(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT) \
	*/

static int flash_range_ok(int offset, int size_req, int align)
{
	if (offset < 0 || size_req < 0 || offset > CONFIG_FLASH_SIZE_BYTES ||
	    size_req > CONFIG_FLASH_SIZE_BYTES ||
	    offset + size_req > CONFIG_FLASH_SIZE_BYTES ||
	    (offset | size_req) & (align - 1))
		return 0; /* Invalid range */

	return 1;
}

#ifdef CONFIG_MAPPED_STORAGE

/**
 * A test public variable allowing us to override the base address of
 * flash_physical_dataptr().
 */
test_export_static const char *flash_physical_dataptr_override;

/**
 * Get the physical memory address of a flash offset
 *
 * This is used for direct flash access. We assume that the flash is
 * contiguous from this start address through to the end of the usable
 * flash.
 *
 * @param offset	Flash offset to get address of
 * @param dataptrp	Returns pointer to memory address of flash offset
 * @return pointer to flash memory offset, if ok, else NULL
 */
static const char *flash_physical_dataptr(int offset)
{
	if (IS_ENABLED(TEST_BUILD) && flash_physical_dataptr_override != NULL) {
		return flash_physical_dataptr_override + offset;
	}
	return (char *)((uintptr_t)CONFIG_MAPPED_STORAGE_BASE + offset);
}

int crec_flash_dataptr(int offset, int size_req, int align, const char **ptrp)
{
	if (!flash_range_ok(offset, size_req, align))
		return -1; /* Invalid range */
	if (ptrp)
		*ptrp = flash_physical_dataptr(offset);

	return CONFIG_FLASH_SIZE_BYTES - offset;
}
#endif

#ifdef CONFIG_FLASH_PSTATE
#ifdef CONFIG_FLASH_PSTATE_BANK

/**
 * Read and return persistent state flags (EC_FLASH_PROTECT_*)
 */
static uint32_t flash_read_pstate(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)flash_physical_dataptr(
			CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_FLAGS) &&
	    (pstate->flags & PERSIST_FLAG_PROTECT_RO)) {
		/* Lock flag is known to be set */
		return EC_FLASH_PROTECT_RO_AT_BOOT;
	} else {
#ifdef CONFIG_WP_ALWAYS
		return PERSIST_FLAG_PROTECT_RO;
#else
		return 0;
#endif
	}
}

/**
 * Write persistent state after erasing.
 *
 * @param pstate	New data to set in pstate. NOT memory mapped
 *                      old pstate as it will be erased.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate_data(struct persist_state *newpstate)
{
	int rv;

	/* Erase pstate */
	rv = crec_flash_physical_erase(CONFIG_FW_PSTATE_OFF,
				       CONFIG_FW_PSTATE_SIZE);
	if (rv)
		return rv;

	/*
	 * Note that if we lose power in here, we'll lose the pstate contents.
	 * That's ok, because it's only possible to write the pstate before
	 * it's protected.
	 */

	/* Write the updated pstate */
	return crec_flash_physical_write(CONFIG_FW_PSTATE_OFF,
					 sizeof(*newpstate),
					 (const char *)newpstate);
}

/**
 * Validate and Init persistent state datastructure.
 *
 * @param pstate	A pstate data structure. Will be valid at complete.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int validate_pstate_struct(struct persist_state *pstate)
{
	if (pstate->version != PERSIST_STATE_VERSION) {
		memset(pstate, 0, sizeof(*pstate));
		pstate->version = PERSIST_STATE_VERSION;
	}

	return EC_SUCCESS;
}

/**
 * Write persistent state from pstate, erasing if necessary.
 *
 * @param flags		New flash write protect flags to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate(uint32_t flags)
{
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)flash_physical_dataptr(
			CONFIG_FW_PSTATE_OFF);

	/* Only check the flags we write to pstate */
	flags &= EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Check if pstate has actually changed */
	if (flags == flash_read_pstate())
		return EC_SUCCESS;

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	if (flags & EC_FLASH_PROTECT_RO_AT_BOOT)
		newpstate.flags |= PERSIST_FLAG_PROTECT_RO;
	else
		newpstate.flags &= ~PERSIST_FLAG_PROTECT_RO;
	newpstate.valid_fields |= PSTATE_VALID_FLAGS;

	return flash_write_pstate_data(&newpstate);
}

#ifdef CONFIG_SERIALNO_LEN
/**
 * Read and return persistent serial number.
 */
const char *crec_flash_read_pstate_serial(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)flash_physical_dataptr(
			CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_SERIALNO)) {
		return (const char *)(pstate->serialno);
	}

	return NULL;
}

/**
 * Write persistent serial number to pstate, erasing if necessary.
 *
 * @param serialno		New ascii serial number to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
int crec_flash_write_pstate_serial(const char *serialno)
{
	int length;
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)flash_physical_dataptr(
			CONFIG_FW_PSTATE_OFF);

	/* Check that this is OK */
	if (!serialno)
		return EC_ERROR_INVAL;

	length = strnlen(serialno, sizeof(newpstate.serialno));
	if (length >= sizeof(newpstate.serialno)) {
		return EC_ERROR_INVAL;
	}

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	/*
	 * Erase any prior data and copy the string. The length was verified to
	 * be shorter than the buffer so a null terminator always remains.
	 */
	memset(newpstate.serialno, '\0', sizeof(newpstate.serialno));
	memcpy(newpstate.serialno, serialno, length);

	newpstate.valid_fields |= PSTATE_VALID_SERIALNO;

	return flash_write_pstate_data(&newpstate);
}

#endif /* CONFIG_SERIALNO_LEN */

#ifdef CONFIG_MAC_ADDR_LEN

/**
 * Read and return persistent MAC address.
 */
const char *crec_flash_read_pstate_mac_addr(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)flash_physical_dataptr(
			CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_MAC_ADDR)) {
		return (const char *)(pstate->mac_addr);
	}

	return NULL;
}

/**
 * Write persistent MAC Addr to pstate, erasing if necessary.
 *
 * @param mac_addr		New ascii MAC address to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
int crec_flash_write_pstate_mac_addr(const char *mac_addr)
{
	int length;
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)flash_physical_dataptr(
			CONFIG_FW_PSTATE_OFF);

	/* Check that this is OK, data is valid and fits in the region. */
	if (!mac_addr) {
		return EC_ERROR_INVAL;
	}

	/*
	 * This will perform validation of the mac address before storing it.
	 * The MAC address format is '12:34:56:78:90:AB', a 17 character long
	 * string containing pairs of hex digits, each pair delimited by a ':'.
	 */
	length = strnlen(mac_addr, sizeof(newpstate.mac_addr));
	if (length != 17) {
		return EC_ERROR_INVAL;
	}
	for (int i = 0; i < 17; i++) {
		if (i % 3 != 2) {
			/* Verify the remaining characters are hex digits. */
			if ((mac_addr[i] < '0' || '9' < mac_addr[i]) &&
			    (mac_addr[i] < 'A' || 'F' < mac_addr[i]) &&
			    (mac_addr[i] < 'a' || 'f' < mac_addr[i])) {
				return EC_ERROR_INVAL;
			}
		} else {
			/* Every 3rd character is a ':' */
			if (mac_addr[i] != ':') {
				return EC_ERROR_INVAL;
			}
		}
	}

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	/*
	 * Erase any prior data and copy the string. The length was verified to
	 * be shorter than the buffer so a null terminator always remains.
	 */
	memset(newpstate.mac_addr, '\0', sizeof(newpstate.mac_addr));
	memcpy(newpstate.mac_addr, mac_addr, length);

	newpstate.valid_fields |= PSTATE_VALID_MAC_ADDR;

	return flash_write_pstate_data(&newpstate);
}

#endif /* CONFIG_MAC_ADDR_LEN */

#else /* !CONFIG_FLASH_PSTATE_BANK */

/**
 * Return the address of the pstate data in EC-RO.
 */
static const uintptr_t get_pstate_addr(void)
{
	uintptr_t addr = (uintptr_t)&pstate_data;

	/* Always use the pstate data in RO, even if we're RW */
	if (system_is_in_rw())
		addr += CONFIG_RO_MEM_OFF - CONFIG_RW_MEM_OFF;

	return addr;
}

/**
 * Read and return persistent state flags (EC_FLASH_PROTECT_*)
 */
static uint32_t flash_read_pstate(void)
{
	/* Check for the unlocked magic value */
	if (*(const uint32_t *)get_pstate_addr() == PSTATE_MAGIC_UNLOCKED)
		return 0;

	/* Anything else is locked */
	return EC_FLASH_PROTECT_RO_AT_BOOT;
}

/**
 * Write persistent state from pstate, erasing if necessary.
 *
 * @param flags		New flash write protect flags to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate(uint32_t flags)
{
	const uint32_t new_pstate = PSTATE_MAGIC_LOCKED;

	/* Only check the flags we write to pstate */
	flags &= EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Check if pstate has actually changed */
	if (flags == flash_read_pstate())
		return EC_SUCCESS;

	/* We can only set the protect flag, not clear it */
	if (!(flags & EC_FLASH_PROTECT_RO_AT_BOOT))
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * Write a new pstate.  We can overwrite the existing value, because
	 * we're only moving bits from the erased state to the unerased state.
	 */
	return crec_flash_physical_write(
		get_pstate_addr() - CONFIG_PROGRAM_MEMORY_BASE,
		sizeof(new_pstate), (const char *)&new_pstate);
}

#endif /* !CONFIG_FLASH_PSTATE_BANK */
#endif /* CONFIG_FLASH_PSTATE */

int crec_flash_is_erased(uint32_t offset, int size)
{
	const uint32_t *ptr;

#ifdef CONFIG_MAPPED_STORAGE
	/* Use pointer directly to flash */
	if (crec_flash_dataptr(offset, size, sizeof(uint32_t),
			       (const char **)&ptr) < 0)
		return 0;

	crec_flash_lock_mapped_storage(1);
	for (size /= sizeof(uint32_t); size > 0; size--, ptr++)
		if (*ptr != CONFIG_FLASH_ERASED_VALUE32) {
			crec_flash_lock_mapped_storage(0);
			return 0;
		}

	crec_flash_lock_mapped_storage(0);
#else
	/* Read flash a chunk at a time */
	uint32_t buf[8];
	int bsize;

	while (size) {
		bsize = MIN(size, sizeof(buf));

		if (crec_flash_read(offset, bsize, (char *)buf))
			return 0;

		size -= bsize;
		offset += bsize;

		ptr = buf;
		for (bsize /= sizeof(uint32_t); bsize > 0; bsize--, ptr++)
			if (*ptr != CONFIG_FLASH_ERASED_VALUE32)
				return 0;
	}
#endif

	return 1;
}

#if defined(CONFIG_ZEPHYR) && defined(CONFIG_PLATFORM_EC_CBI_FLASH)
/**
 * Check if the passed section overlaps with CBI section on EC flash.
 *
 * @param offset	Flash offset.
 * @param size		Length of section in bytes.
 * @return true if there is overlap, or false if there is no overlap.
 */
static bool check_cbi_section_overlap(int offset, int size)
{
	int cbi_start = CBI_FLASH_OFFSET;
	int cbi_end = CBI_FLASH_OFFSET + CBI_FLASH_SIZE;
	int sec_start = offset;
	int sec_end = offset + size;

	return !((sec_end <= cbi_start) || (sec_start >= cbi_end));
}

/**
 * Hide the information related to CBI(EC flash) if data contains any.
 *
 * @param offset	Flash offset.
 * @param size		Length of section in bytes.
 * @param data		Flash data.  Must be 32-bit aligned.
 */
static void protect_cbi_overlapped_section(int offset, int size, char *data)
{
	if (check_cbi_section_overlap(offset, size)) {
		int cbi_end = CBI_FLASH_OFFSET + CBI_FLASH_SIZE;
		int sec_end = offset + size;
		int cbi_fill_start = MAX(CBI_FLASH_OFFSET, offset);
		int cbi_fill_size = MIN(cbi_end, sec_end) - cbi_fill_start;

		memset(data + (cbi_fill_start - offset), 0xff, cbi_fill_size);
	}
}
#endif

test_mockable int crec_flash_unprotected_read(int offset, int size, char *data)
{
#ifdef CONFIG_MAPPED_STORAGE
	const char *src;

	if (crec_flash_dataptr(offset, size, 1, &src) < 0)
		return EC_ERROR_INVAL;

	crec_flash_lock_mapped_storage(1);
	memcpy(data, src, size);
	crec_flash_lock_mapped_storage(0);
	return EC_SUCCESS;
#else
	return crec_flash_physical_read(offset, size, data);
#endif
}

int crec_flash_read(int offset, int size, char *data)
{
	RETURN_ERROR(crec_flash_unprotected_read(offset, size, data));
#if defined(CONFIG_ZEPHYR) && defined(CONFIG_PLATFORM_EC_CBI_FLASH)
	protect_cbi_overlapped_section(offset, size, data);
#endif
	return EC_SUCCESS;
}

static void flash_abort_or_invalidate_hash(int offset, int size)
{
#ifdef CONFIG_VBOOT_HASH
	if (vboot_hash_in_progress()) {
		/* Abort hash calculation when flash update is in progress. */
		vboot_hash_abort();
		return;
	}

#ifdef CONFIG_EXTERNAL_STORAGE
	/*
	 * If EC executes in RAM and is currently in RW, we keep the current
	 * hash. On the next hash check, AP will catch hash mismatch between the
	 * flash copy and the RAM copy, then take necessary actions.
	 */
	if (system_is_in_rw())
		return;
#endif

	/* If EC executes in place, we need to invalidate the cached hash. */
	vboot_hash_invalidate(offset, size);
#endif

#ifdef HAS_TASK_RWSIG
	/*
	 * If RW flash has been written to, make sure we do not automatically
	 * jump to RW after the timeout.
	 */
	if ((offset >= CONFIG_EC_WRITABLE_STORAGE_OFF &&
	     offset < (CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_SIZE)) ||
	    ((offset + size) > CONFIG_EC_WRITABLE_STORAGE_OFF &&
	     (offset + size) <=
		     (CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_SIZE)) ||
	    (offset < CONFIG_EC_WRITABLE_STORAGE_OFF &&
	     (offset + size) >
		     (CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_SIZE)))
		rwsig_abort();
#endif
}

int crec_flash_write(int offset, int size, const char *data)
{
	if (!flash_range_ok(offset, size, CONFIG_FLASH_WRITE_SIZE))
		return EC_ERROR_INVAL; /* Invalid range */

	flash_abort_or_invalidate_hash(offset, size);

#if defined(CONFIG_ZEPHYR) && defined(CONFIG_PLATFORM_EC_CBI_FLASH)
	if (check_cbi_section_overlap(offset, size)) {
		int cbi_end = CBI_FLASH_OFFSET + CBI_FLASH_SIZE;
		int sec_end = offset + size;

		if (offset < CBI_FLASH_OFFSET) {
			RETURN_ERROR(crec_flash_physical_write(
				offset, CBI_FLASH_OFFSET - offset, data));
		}
		if (sec_end > cbi_end) {
			RETURN_ERROR(crec_flash_physical_write(
				cbi_end, sec_end - cbi_end,
				data + cbi_end - offset));
		}
		return EC_SUCCESS;
	}
#endif
	return crec_flash_physical_write(offset, size, data);
}

int crec_flash_erase(int offset, int size)
{
#ifndef CONFIG_FLASH_MULTIPLE_REGION
	if (!flash_range_ok(offset, size, CONFIG_FLASH_ERASE_SIZE))
		return EC_ERROR_INVAL; /* Invalid range */
#endif

	flash_abort_or_invalidate_hash(offset, size);

#if defined(CONFIG_ZEPHYR) && defined(CONFIG_PLATFORM_EC_CBI_FLASH)
	if (check_cbi_section_overlap(offset, size)) {
		int cbi_end = CBI_FLASH_OFFSET + CBI_FLASH_SIZE;
		int sec_end = offset + size;

		if (offset < CBI_FLASH_OFFSET) {
			RETURN_ERROR(crec_flash_physical_erase(
				offset, CBI_FLASH_OFFSET - offset));
		}
		if (sec_end > cbi_end) {
			RETURN_ERROR(crec_flash_physical_erase(
				cbi_end, sec_end - cbi_end));
		}
		return EC_SUCCESS;
	}
#endif
	return crec_flash_physical_erase(offset, size);
}

int crec_flash_protect_at_boot(uint32_t new_flags)
{
#ifdef CONFIG_FLASH_PSTATE
	uint32_t new_pstate_flags = new_flags & EC_FLASH_PROTECT_RO_AT_BOOT;

	/* Read the current persist state from flash */
	if (flash_read_pstate() != new_pstate_flags) {
		/* Need to update pstate */
		int rv;

#ifdef CONFIG_FLASH_PSTATE_BANK
		/* Fail if write protect block is already locked */
		if (crec_flash_physical_get_protect(PSTATE_BANK))
			return EC_ERROR_ACCESS_DENIED;
#endif

		/* Write the desired flags */
		rv = flash_write_pstate(new_pstate_flags);
		if (rv)
			return rv;
	}

#ifdef CONFIG_FLASH_PROTECT_NEXT_BOOT
	/*
	 * Try updating at-boot protection state, if on a platform where write
	 * protection only changes after a reboot.  Otherwise we wouldn't
	 * update it until after the next reboot, and we'd need to reboot
	 * again.  Ignore errors, because the protection registers might
	 * already be locked this boot, and we'll still apply the correct state
	 * again on the next boot.
	 *
	 * This assumes PSTATE immediately follows RO, which it does on
	 * all STM32 platforms (which are the only ones with this config).
	 */
	crec_flash_physical_protect_at_boot(new_flags);
#endif

	return EC_SUCCESS;
#else
	return crec_flash_physical_protect_at_boot(new_flags);
#endif
}

uint32_t crec_flash_get_protect(void)
{
	uint32_t flags = 0;
	int i;
	/* Region protection status */
	int not_protected[FLASH_REGION_COUNT] = { 0 };
#ifdef CONFIG_ROLLBACK
	/* Flags that must be set to set ALL_NOW flag. */
	const uint32_t all_flags = EC_FLASH_PROTECT_RO_NOW |
				   EC_FLASH_PROTECT_RW_NOW |
				   EC_FLASH_PROTECT_ROLLBACK_NOW;
#else
	const uint32_t all_flags = EC_FLASH_PROTECT_RO_NOW |
				   EC_FLASH_PROTECT_RW_NOW;
#endif

	/* Read write protect GPIO */
	if (write_protect_is_asserted())
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;

#ifdef CONFIG_FLASH_PSTATE
	/* Read persistent state of RO-at-boot flag */
	flags |= flash_read_pstate();
#endif

	/* Scan flash protection */
	for (i = 0; i < crec_flash_total_banks(); i++) {
		int is_ro = (i >= WP_BANK_OFFSET &&
			     i < WP_BANK_OFFSET + WP_BANK_COUNT);
		enum flash_region region = is_ro ? FLASH_REGION_RO :
						   FLASH_REGION_RW;
		int bank_flag = is_ro ? EC_FLASH_PROTECT_RO_NOW :
					EC_FLASH_PROTECT_RW_NOW;

#ifdef CONFIG_ROLLBACK
		if (i >= ROLLBACK_BANK_OFFSET &&
		    i < ROLLBACK_BANK_OFFSET + ROLLBACK_BANK_COUNT) {
			region = FLASH_REGION_ROLLBACK;
			bank_flag = EC_FLASH_PROTECT_ROLLBACK_NOW;
		}
#endif

		if (crec_flash_physical_get_protect(i)) {
			/* At least one bank in the region is protected */
			flags |= bank_flag;
			if (not_protected[region])
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		} else {
			/* At least one bank in the region is NOT protected */
			not_protected[region] = 1;
			if (flags & bank_flag)
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		}
	}

	if ((flags & all_flags) == all_flags)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	/*
	 * If the RW or ROLLBACK banks are protected but the RO banks aren't,
	 * that's inconsistent.
	 *
	 * Note that we check this before adding in the physical flags below,
	 * since some chips can also protect ALL_NOW for the current boot by
	 * locking up the flash program-erase registers.
	 */
	if ((flags & all_flags) && !(flags & EC_FLASH_PROTECT_RO_NOW))
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

#ifndef CONFIG_FLASH_PROTECT_RW
	/* RW flag was used for intermediate computations, clear it now. */
	flags &= ~EC_FLASH_PROTECT_RW_NOW;
#endif

	/* Add in flags from physical layer */
	return flags | crec_flash_physical_get_protect_flags();
}

/*
 * Request a flash protection flags change for |mask| flash protect flags
 * to |flags| state.
 *
 * Order of flag processing:
 * 1. Clear/Set RO_AT_BOOT + Clear *_AT_BOOT flags + Commit *_AT_BOOT flags.
 * 2. Return if RO_AT_BOOT and HW-WP are not asserted.
 * 3. Set remaining *_AT_BOOT flags + Commit *_AT_BOOT flags.
 * 4. Commit RO_NOW.
 * 5. Commit ALL_NOW.
 */
int crec_flash_set_protect(uint32_t mask, uint32_t flags)
{
	int retval = EC_SUCCESS;
	int rv;
	int old_flags_at_boot =
		crec_flash_get_protect() &
		(EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RW_AT_BOOT |
		 EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
		 EC_FLASH_PROTECT_ALL_AT_BOOT);
	int new_flags_at_boot = old_flags_at_boot;

	/* Sanitize input flags */
	flags = flags & mask;

	/*
	 * Process flags we can set.  Track the most recent error, but process
	 * all flags before returning.
	 */

	/*
	 * AT_BOOT flags are trickier than NOW flags, as they can be set
	 * when HW write protection is disabled and can be unset without
	 * a reboot.
	 *
	 * If we are only setting/clearing RO_AT_BOOT, things are simple.
	 * Setting ALL_AT_BOOT is processed only if HW write protection is
	 * enabled and RO_AT_BOOT is set, so it's also simple.
	 *
	 * The most tricky one is when we want to clear ALL_AT_BOOT. We need
	 * to determine whether to clear protection for the entire flash or
	 * leave RO protected. There are two cases that we want to keep RO
	 * protected:
	 *   A. RO_AT_BOOT was already set before flash_set_protect() is
	 *      called.
	 *   B. RO_AT_BOOT was not set, but it's requested to be set by
	 *      the caller of flash_set_protect().
	 */

	/* 1.a - Clear RO_AT_BOOT. */
	new_flags_at_boot &= ~(mask & EC_FLASH_PROTECT_RO_AT_BOOT);
	/* 1.b - Set RO_AT_BOOT. */
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_RO_AT_BOOT;

	/* 1.c - Clear ALL_AT_BOOT. */
	if ((mask & EC_FLASH_PROTECT_ALL_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_ALL_AT_BOOT)) {
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ALL_AT_BOOT;
		/* Must also clear RW/ROLLBACK. */
#ifdef CONFIG_FLASH_PROTECT_RW
		new_flags_at_boot &= ~EC_FLASH_PROTECT_RW_AT_BOOT;
#endif
#ifdef CONFIG_ROLLBACK
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif
	}

	/* 1.d - Clear RW_AT_BOOT. */
#ifdef CONFIG_FLASH_PROTECT_RW
	if ((mask & EC_FLASH_PROTECT_RW_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_RW_AT_BOOT)) {
		new_flags_at_boot &= ~EC_FLASH_PROTECT_RW_AT_BOOT;
		/* Must also clear ALL (otherwise nothing will happen). */
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ALL_AT_BOOT;
	}
#endif

	/* 1.e - Clear ROLLBACK_AT_BOOT. */
#ifdef CONFIG_ROLLBACK
	if ((mask & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT)) {
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
		/* Must also remove ALL (otherwise nothing will happen). */
		new_flags_at_boot &= ~EC_FLASH_PROTECT_ALL_AT_BOOT;
	}
#endif

	/* 1.f - Commit *_AT_BOOT "clears" (and RO "set" 1.b). */
	if (new_flags_at_boot != old_flags_at_boot) {
		rv = crec_flash_protect_at_boot(new_flags_at_boot);
		if (rv)
			retval = rv;
		old_flags_at_boot = new_flags_at_boot;
	}

	/* 2 - Return if RO_AT_BOOT and HW-WP are not asserted.
	 *
	 * All subsequent flags only work if write protect is enabled (that is,
	 * hardware WP flag) *and* RO is protected at boot (software WP flag).
	 */
	if ((~crec_flash_get_protect()) &
	    (EC_FLASH_PROTECT_GPIO_ASSERTED | EC_FLASH_PROTECT_RO_AT_BOOT))
		return retval;

	/*
	 * 3.a - Set ALL_AT_BOOT.
	 *
	 * The case where ALL/RW/ROLLBACK_AT_BOOT is cleared is already covered
	 * above, so we do not need to mask it out.
	 */
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_ALL_AT_BOOT;

	/* 3.b - Set RW_AT_BOOT. */
#ifdef CONFIG_FLASH_PROTECT_RW
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

	/* 3.c - Set ROLLBACK_AT_BOOT. */
#ifdef CONFIG_ROLLBACK
	new_flags_at_boot |= flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif

	/* 3.d - Commit *_AT_BOOT "sets". */
	if (new_flags_at_boot != old_flags_at_boot) {
		rv = crec_flash_protect_at_boot(new_flags_at_boot);
		if (rv)
			retval = rv;
	}

	/* 4 - Commit RO_NOW. */
	if (flags & EC_FLASH_PROTECT_RO_NOW) {
		rv = crec_flash_physical_protect_now(0);
		if (rv)
			retval = rv;

		/*
		 * Latch the CBI EEPROM WP immediately if HW WP is asserted and
		 * we're now protecting the RO region with SW WP.
		 */
		if (IS_ENABLED(CONFIG_EEPROM_CBI_WP) &&
		    (EC_FLASH_PROTECT_GPIO_ASSERTED & crec_flash_get_protect()))
			cbi_latch_eeprom_wp();
	}

	/* 5 - Commit ALL_NOW. */
	if (flags & EC_FLASH_PROTECT_ALL_NOW) {
		rv = crec_flash_physical_protect_now(1);
		if (rv)
			retval = rv;
	}

	return retval;
}

#ifdef CONFIG_FLASH_DEFERRED_ERASE
static volatile enum ec_status erase_rc = EC_RES_SUCCESS;
static struct ec_params_flash_erase_v1 erase_info;

static void flash_erase_deferred(void)
{
	if (crec_flash_erase(erase_info.params.offset, erase_info.params.size))
		erase_rc = EC_RES_ERROR;
	else
		erase_rc = EC_RES_SUCCESS;
}
DECLARE_DEFERRED(flash_erase_deferred);
#endif

#if !defined(CONFIG_ZEPHYR) || \
	!defined(CONFIG_PLATFORM_EC_USE_ZEPHYR_FLASH_PAGE_LAYOUT)
void crec_flash_print_region_info(void)
{
#ifdef CONFIG_FLASH_MULTIPLE_REGION
	int i;

	ccprintf("Regions:\n");
	for (i = 0; i < ARRAY_SIZE(flash_bank_array); i++) {
		ccprintf(" %d region%s:\n", flash_bank_array[i].count,
			 (flash_bank_array[i].count == 1 ? "" : "s"));
		ccprintf("  Erase:   %4d B (to %d-bits)\n",
			 1 << flash_bank_array[i].erase_size_exp,
			 CONFIG_FLASH_ERASED_VALUE32 ? 1 : 0);
		ccprintf("  Size/Protect: %4d B\n",
			 1 << flash_bank_array[i].size_exp);
	}
#else
	ccprintf("Erase:   %4d B (to %d-bits)\n", CONFIG_FLASH_ERASE_SIZE,
		 CONFIG_FLASH_ERASED_VALUE32 ? 1 : 0);
	ccprintf("Protect: %4d B\n", CONFIG_FLASH_BANK_SIZE);
#endif
}
#endif

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_FLASHINFO
#define BIT_TO_ON_OFF(value, mask) \
	((((value) & (mask)) == (mask)) ? "ON" : "OFF")
static int command_flash_info(int argc, const char **argv)
{
	int i, flags;

	ccprintf("Usable:  %4d KB\n", CONFIG_FLASH_SIZE_BYTES / 1024);
	ccprintf("Write:   %4d B (ideal %d B)\n", CONFIG_FLASH_WRITE_SIZE,
		 CONFIG_FLASH_WRITE_IDEAL_SIZE);
	crec_flash_print_region_info();
	flags = crec_flash_get_protect();
	ccprintf("Flags:\n");
	ccprintf("  wp_gpio_asserted: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_GPIO_ASSERTED));
	ccprintf("  ro_at_boot: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_RO_AT_BOOT));
	ccprintf("  all_at_boot: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ALL_AT_BOOT));
	ccprintf("  ro_now: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_RO_NOW));
	ccprintf("  all_now: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ALL_NOW));
#ifdef CONFIG_FLASH_PROTECT_RW
	ccprintf("  rw_at_boot: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_RW_AT_BOOT));
	ccprintf("  rw_now: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_RW_NOW));
#endif
	ccprintf("  STUCK: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ERROR_STUCK));
	ccprintf("  INCONSISTENT: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ERROR_INCONSISTENT));
	ccprintf("  UNKNOWN_ERROR: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ERROR_UNKNOWN));
#ifdef CONFIG_ROLLBACK
	ccprintf("  rollback_at_boot: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ROLLBACK_AT_BOOT));
	ccprintf("  rollback_now: %s\n",
		 BIT_TO_ON_OFF(flags, EC_FLASH_PROTECT_ROLLBACK_NOW));
#endif

	ccputs("Protected now:");
	for (i = 0; i < crec_flash_total_banks(); i++) {
		if (!(i & 31))
			ccputs("\n    ");
		else if (!(i & 7))
			ccputs(" ");
		ccputs(crec_flash_physical_get_protect(i) ? "Y" : ".");
	}
	ccputs("\n");
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(flashinfo, command_flash_info, NULL,
			     "Print flash info");
#endif /* CONFIG_CMD_FLASHINFO */

#ifdef CONFIG_CMD_FLASH
static int command_flash_erase(int argc, const char **argv)
{
	int offset = -1;
	int size = -1;
	int rv;

	if (crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_ERROR_ACCESS_DENIED;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x...\n", size, offset);
	return crec_flash_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase, "offset size",
			"Erase flash");

static int command_flash_write(int argc, const char **argv)
{
	int offset = -1;
	int size = -1;
	int rv;
	char *data;
	int i;

	if (crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_ERROR_ACCESS_DENIED;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = i;

	ccprintf("Writing %d bytes to 0x%x...\n", size, offset);
	rv = crec_flash_write(offset, size, data);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write, "offset size",
			"Write pattern to flash");

static int command_flash_read(int argc, const char **argv)
{
	int offset = -1;
	int size = 256;
	int rv;
	uint8_t *data;
	int i;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, (char **)&data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Read the data */
	if (crec_flash_read(offset, size, data)) {
		shared_mem_release(data);
		return EC_ERROR_INVAL;
	}

	/* Dump it */
	for (i = 0; i < size; i++) {
		if ((offset + i) % 16) {
			ccprintf(" %02x", data[i]);
		} else {
			ccprintf("\n%08x: %02x", offset + i, data[i]);
			cflush();
		}
	}
	ccprintf("\n");

	/* Free the buffer */
	shared_mem_release(data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashread, command_flash_read, "offset [size]",
			"Read flash");
#endif

#ifdef CONFIG_CMD_FLASH_WP
static int command_flash_wp(int argc, const char **argv)
{
	int val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "now"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_ALL_NOW, -1);

	if (!strcasecmp(argv[1], "all"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, -1);

	if (!strcasecmp(argv[1], "noall"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, 0);

#ifdef CONFIG_FLASH_PROTECT_RW
	if (!strcasecmp(argv[1], "rw"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, -1);

	if (!strcasecmp(argv[1], "norw"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_RW_AT_BOOT, 0);
#endif

#ifdef CONFIG_ROLLBACK
	if (!strcasecmp(argv[1], "rb"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_ROLLBACK_AT_BOOT,
					      -1);

	if (!strcasecmp(argv[1], "norb"))
		return crec_flash_set_protect(EC_FLASH_PROTECT_ROLLBACK_AT_BOOT,
					      0);
#endif

	/* Do this last, since anything starting with 'n' means "no" */
	if (parse_bool(argv[1], &val))
		return crec_flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
					      val ? -1 : 0);

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp,
			"<BOOLEAN> | now | all | noall"
#ifdef CONFIG_FLASH_PROTECT_RW
			" | rw | norw"
#endif
#ifdef CONFIG_ROLLBACK
			" | rb | norb"
#endif
			,
			"Modify flash write protect");
#endif /* CONFIG_CMD_FLASH_WP */

/*****************************************************************************/
/* Host commands */

/*
 * All internal EC code assumes that offsets are provided relative to
 * physical address zero of storage. In some cases, the region of storage
 * belonging to the EC is not physical address zero - a non-zero fmap_base
 * indicates so. Since fmap_base is not yet handled correctly by external
 * code, we must perform the adjustment in our host command handlers -
 * adjust all offsets so they are relative to the beginning of the storage
 * region belonging to the EC. TODO(crbug.com/529365): Handle fmap_base
 * correctly in flashrom, dump_fmap, etc. and remove EC_FLASH_REGION_START.
 */
#define EC_FLASH_REGION_START \
	MIN(CONFIG_EC_PROTECTED_STORAGE_OFF, CONFIG_EC_WRITABLE_STORAGE_OFF)

static enum ec_status flash_command_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_info_2 *p_2 = args->params;
	struct ec_response_flash_info_2 *r_2 = args->response;
#ifndef CONFIG_FLASH_MULTIPLE_REGION
	struct ec_response_flash_info_1 *r_1 = args->response;
#endif
	int res;

	/*
	 * Compute the ideal amount of data for the host to send us,
	 * based on the maximum response size and the ideal write size.
	 */
	int ideal_size =
		(args->response_max - sizeof(struct ec_params_flash_write)) &
		~(CONFIG_FLASH_WRITE_IDEAL_SIZE - 1);
	/*
	 * If we can't get at least one ideal block, then just want
	 * as high a multiple of the minimum write size as possible.
	 */
	if (!ideal_size)
		ideal_size = (args->response_max -
			      sizeof(struct ec_params_flash_write)) &
			     ~(CONFIG_FLASH_WRITE_SIZE - 1);

	if (args->version >= 2) {
		args->response_size = sizeof(struct ec_response_flash_info_2);
		r_2->flash_size =
			CONFIG_FLASH_SIZE_BYTES - EC_FLASH_REGION_START;
#if (CONFIG_FLASH_ERASED_VALUE32 == 0)
		r_2->flags = EC_FLASH_INFO_ERASE_TO_0;
#else
		r_2->flags = 0;
#endif
#ifdef CONFIG_FLASH_SELECT_REQUIRED
		r_2->flags |= EC_FLASH_INFO_SELECT_REQUIRED;
#endif
		r_2->write_ideal_size = ideal_size;
		/*
		 * Fill r_2->num_banks_desc, r_2->num_banks_total and
		 * r_2->banks.
		 */
		res = crec_flash_response_fill_banks(r_2, p_2->num_banks_desc);
		if (res != EC_RES_SUCCESS)
			return res;

		args->response_size +=
			r_2->num_banks_desc * sizeof(struct ec_flash_bank);
		return EC_RES_SUCCESS;
	}
#ifdef CONFIG_FLASH_MULTIPLE_REGION
	return EC_RES_INVALID_PARAM;
#else
	r_1->flash_size = CONFIG_FLASH_SIZE_BYTES - EC_FLASH_REGION_START;
	r_1->flags = 0;
	r_1->write_block_size = CONFIG_FLASH_WRITE_SIZE;
	r_1->erase_block_size = CONFIG_FLASH_ERASE_SIZE;
	r_1->protect_block_size = CONFIG_FLASH_BANK_SIZE;
	if (args->version == 0) {
		/* Only version 0 fields returned */
		args->response_size = sizeof(struct ec_response_flash_info);
	} else {
		args->response_size = sizeof(struct ec_response_flash_info_1);
		/* Fill in full version 1 struct */
		r_1->write_ideal_size = ideal_size;
#if (CONFIG_FLASH_ERASED_VALUE32 == 0)
		r_1->flags |= EC_FLASH_INFO_ERASE_TO_0;
#endif
#ifdef CONFIG_FLASH_SELECT_REQUIRED
		r_1->flags |= EC_FLASH_INFO_SELECT_REQUIRED;
#endif
	}
	return EC_RES_SUCCESS;
#endif /* CONFIG_FLASH_MULTIPLE_REGION */
}
#ifdef CONFIG_FLASH_MULTIPLE_REGION
#define FLASH_INFO_VER EC_VER_MASK(2)
#else
#define FLASH_INFO_VER (EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2))
#endif
DECLARE_HOST_COMMAND(EC_CMD_FLASH_INFO, flash_command_get_info, FLASH_INFO_VER);

static enum ec_status flash_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_read *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (p->size > args->response_max)
		return EC_RES_OVERFLOW;

	if (crec_flash_read(offset, p->size, args->response))
		return EC_RES_ERROR;

	args->response_size = p->size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_READ, flash_command_read, EC_VER_MASK(0));

/**
 * Flash write command
 *
 * Version 0 and 1 are equivalent from the EC-side; the only difference is
 * that the host can only send 64 bytes of data at a time in version 0.
 */
static enum ec_status flash_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_write *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_RES_ACCESS_DENIED;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#ifdef CONFIG_INTERNAL_STORAGE
	if (system_unsafe_to_overwrite(offset, p->size))
		return EC_RES_ACCESS_DENIED;
#endif

	if (crec_flash_write(offset, p->size, (const uint8_t *)(p + 1)))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_WRITE, flash_command_write,
		     EC_VER_MASK(0) | EC_VER_MASK(EC_VER_FLASH_WRITE));

#ifndef CONFIG_FLASH_MULTIPLE_REGION
/*
 * Make sure our image sizes are a multiple of flash block erase size so that
 * the host can erase the entire image.
 * Note that host (flashrom/depthcharge) does not erase/program the
 * EC_FLASH_REGION_RO region, it only queries this region.
 */
BUILD_ASSERT(CONFIG_WP_STORAGE_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);
BUILD_ASSERT(CONFIG_EC_WRITABLE_STORAGE_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);

#endif

static enum ec_status flash_command_erase(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_erase *p = args->params;
	int rc = EC_RES_SUCCESS, cmd = FLASH_ERASE_SECTOR;
	uint32_t offset;
#ifdef CONFIG_FLASH_DEFERRED_ERASE
	const struct ec_params_flash_erase_v1 *p_1 = args->params;

	if (args->version > 0) {
		cmd = p_1->cmd;
		p = &p_1->params;
	}
#endif
	offset = p->offset + EC_FLASH_REGION_START;

	if (crec_flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_RES_ACCESS_DENIED;

#ifdef CONFIG_INTERNAL_STORAGE
	if (system_unsafe_to_overwrite(offset, p->size))
		return EC_RES_ACCESS_DENIED;
#endif

	switch (cmd) {
	case FLASH_ERASE_SECTOR:
#if defined(HAS_TASK_HOSTCMD) && defined(CONFIG_HOST_COMMAND_STATUS)
#ifndef CONFIG_EC_HOST_CMD
		args->result = EC_RES_IN_PROGRESS;
		host_send_response(args);
#else
		ec_host_cmd_send_response(
			EC_HOST_CMD_IN_PROGRESS,
			(struct ec_host_cmd_handler_args *)args);
#endif
#endif
		if (crec_flash_erase(offset, p->size))
			return EC_RES_ERROR;

		break;
#ifdef CONFIG_FLASH_DEFERRED_ERASE
	case FLASH_ERASE_SECTOR_ASYNC:
		rc = erase_rc;
		if (rc == EC_RES_SUCCESS) {
			memcpy(&erase_info, p_1, sizeof(*p_1));
			hook_call_deferred(&flash_erase_deferred_data,
					   100 * MSEC);
			erase_rc = EC_RES_BUSY;
		} else {
			/*
			 * Not our job to return the result of
			 * the previous command.
			 */
			rc = EC_RES_BUSY;
		}
		break;
	case FLASH_ERASE_GET_RESULT:
		rc = erase_rc;
		if (rc != EC_RES_BUSY)
			/* Ready for another command */
			erase_rc = EC_RES_SUCCESS;
		break;
#endif
	default:
		rc = EC_RES_INVALID_PARAM;
	}
	return rc;
}

DECLARE_HOST_COMMAND(EC_CMD_FLASH_ERASE, flash_command_erase,
		     EC_VER_MASK(0)
#ifdef CONFIG_FLASH_DEFERRED_ERASE
			     | EC_VER_MASK(1)
#endif
);

#ifdef CONFIG_FLASH_PROTECT_DEFERRED
struct flash_protect_async {
	uint32_t mask;
	uint32_t flags;

	volatile enum ec_status rc;
} __ec_align4;

static struct flash_protect_async flash_protect_async_data = {
	.rc = EC_RES_SUCCESS
};

static void crec_flash_set_protect_deferred(void)
{
	if (crec_flash_set_protect(flash_protect_async_data.mask,
				   flash_protect_async_data.flags))
		flash_protect_async_data.rc = EC_RES_ERROR;
	else
		flash_protect_async_data.rc = EC_RES_SUCCESS;
}
DECLARE_DEFERRED(crec_flash_set_protect_deferred);

static enum ec_status
flash_command_protect_v2(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_protect_v2 *p = args->params;
	struct ec_response_flash_protect *r = args->response;
	int rc;

	flash_protect_async_data.mask = p->mask;
	flash_protect_async_data.flags = p->flags;

	/*
	 * Handle requesting new flags.  Note that we ignore the return code
	 * from flash_set_protect(), since errors will be visible to the caller
	 * via the flags in the response.  (If we returned error, the caller
	 * wouldn't get the response.)
	 */

	switch (p->action) {
	case FLASH_PROTECT_ASYNC:
		rc = flash_protect_async_data.rc;
		if (rc == EC_RES_BUSY) {
			return rc;
		}
		if (p->mask) {
			hook_call_deferred(
				&crec_flash_set_protect_deferred_data,
				100 * MSEC);
			/* Not our job to return the result of
			 * the previous command.
			 */
			flash_protect_async_data.rc = EC_RES_BUSY;
		}
		return EC_RES_SUCCESS;

	case FLASH_PROTECT_GET_RESULT:
		/*
		 * Retrieve the current flags.  The caller can use this
		 * to determine which of the requested flags could be
		 * set.  This is cleaner than simply returning error,
		 * because it provides information to the caller about
		 * the actual result.
		 */
		rc = flash_protect_async_data.rc;
		if (rc == EC_RES_ERROR) {
			/* Ready for another command */
			flash_protect_async_data.rc = EC_RES_SUCCESS;
			break;
		}

		if (rc == EC_RES_BUSY) {
			break;
		}

		r->flags = crec_flash_get_protect();

		/* Indicate which flags are valid on this platform */
		r->valid_flags = EC_FLASH_PROTECT_GPIO_ASSERTED |
				 EC_FLASH_PROTECT_ERROR_STUCK |
				 EC_FLASH_PROTECT_ERROR_INCONSISTENT |
				 EC_FLASH_PROTECT_ERROR_UNKNOWN |
				 crec_flash_physical_get_valid_flags();
		r->writable_flags =
			crec_flash_physical_get_writable_flags(r->flags);

		args->response_size = sizeof(*r);

		break;

	default:
		rc = EC_RES_INVALID_PARAM;
	}

	return rc;
}
#endif

static enum ec_status flash_command_protect(struct host_cmd_handler_args *args)
{
#if defined(CONFIG_FLASH_PROTECT_DEFERRED)
	if (args->version == 2) {
		return flash_command_protect_v2(args);
	}
#endif

	const struct ec_params_flash_protect *p = args->params;
	struct ec_response_flash_protect *r = args->response;

	/*
	 * Handle requesting new flags.  Note that we ignore the return code
	 * from flash_set_protect(), since errors will be visible to the caller
	 * via the flags in the response.  (If we returned error, the caller
	 * wouldn't get the response.)
	 */
	if (p->mask)
		crec_flash_set_protect(p->mask, p->flags);

	/*
	 * Retrieve the current flags.  The caller can use this to determine
	 * which of the requested flags could be set.  This is cleaner than
	 * simply returning error, because it provides information to the
	 * caller about the actual result.
	 */
	r->flags = crec_flash_get_protect();

	/* Indicate which flags are valid on this platform */
	r->valid_flags = EC_FLASH_PROTECT_GPIO_ASSERTED |
			 EC_FLASH_PROTECT_ERROR_STUCK |
			 EC_FLASH_PROTECT_ERROR_INCONSISTENT |
			 EC_FLASH_PROTECT_ERROR_UNKNOWN |
			 crec_flash_physical_get_valid_flags();
	r->writable_flags = crec_flash_physical_get_writable_flags(r->flags);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_FLASH_PROTECT, flash_command_protect,
		     EC_VER_MASK(1)
#ifdef CONFIG_FLASH_PROTECT_DEFERRED
			     | EC_VER_MASK(2)
#endif
);

static enum ec_status
flash_command_region_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_region_info *p = args->params;
	struct ec_response_flash_region_info *r = args->response;

	switch (p->region) {
	case EC_FLASH_REGION_RO:
		r->offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
			    CONFIG_RO_STORAGE_OFF - EC_FLASH_REGION_START;
		r->size = EC_FLASH_REGION_RO_SIZE;
		break;
	case EC_FLASH_REGION_ACTIVE:
		r->offset = flash_get_rw_offset(system_get_active_copy()) -
			    EC_FLASH_REGION_START;
		r->size = CONFIG_EC_WRITABLE_STORAGE_SIZE;
		break;
	case EC_FLASH_REGION_WP_RO:
		r->offset = CONFIG_WP_STORAGE_OFF - EC_FLASH_REGION_START;
		r->size = CONFIG_WP_STORAGE_SIZE;
		break;
	case EC_FLASH_REGION_UPDATE:
		r->offset = flash_get_rw_offset(system_get_update_copy()) -
			    EC_FLASH_REGION_START;
		r->size = CONFIG_EC_WRITABLE_STORAGE_SIZE;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_REGION_INFO, flash_command_region_info,
		     EC_VER_MASK(EC_VER_FLASH_REGION_INFO));

#ifdef CONFIG_FLASH_SELECT_REQUIRED

static enum ec_status flash_command_select(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_select *p = args->params;

	return crec_board_flash_select(p->select);
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_SELECT, flash_command_select, EC_VER_MASK(0));

#endif /* CONFIG_FLASH_SELECT_REQUIRED */
