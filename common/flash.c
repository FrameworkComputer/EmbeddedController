/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC - common functions */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "gpio.h"
#include "host_command.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"
#include "vboot_hash.h"

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
#define PERSIST_STATE_VERSION 3  /* Expected persist_state.version */
#define SERIALNO_MAX 30

/* Flags for persist_state.flags */
/* Protect persist state and RO firmware at boot */
#define PERSIST_FLAG_PROTECT_RO 0x02
#define PSTATE_VALID_FLAGS	(1 << 0)
#define PSTATE_VALID_SERIALNO	(1 << 1)

struct persist_state {
	uint8_t version;            /* Version of this struct */
	uint8_t flags;              /* Lock flags (PERSIST_FLAG_*) */
	uint8_t valid_fields;       /* Flags for valid data. */
	uint8_t reserved;           /* Reserved; set 0 */
	uint8_t serialno[SERIALNO_MAX]; /* Serial number. */
};

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
#define PSTATE_MAGIC_UNLOCKED 0x4f4e5057  /* "WPNO" */
#define PSTATE_MAGIC_LOCKED   0x00000000  /* ""     */
#elif (CONFIG_FLASH_ERASED_VALUE32 == 0)
#define PSTATE_MAGIC_UNLOCKED 0x4f4e5057  /* "WPNO" */
#define PSTATE_MAGIC_LOCKED   0x5f5f5057  /* "WP__" */
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
	PSTATE_MAGIC_UNLOCKED;

#endif /* !CONFIG_FLASH_PSTATE_BANK */
#endif /* CONFIG_FLASH_PSTATE */

int flash_range_ok(int offset, int size_req, int align)
{
	if (offset < 0 || size_req < 0 ||
			offset + size_req > CONFIG_FLASH_SIZE ||
			(offset | size_req) & (align - 1))
		return 0;  /* Invalid range */

	return 1;
}

#ifdef CONFIG_MAPPED_STORAGE
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
	return (char *)((uintptr_t)CONFIG_MAPPED_STORAGE_BASE + offset);
}

int flash_dataptr(int offset, int size_req, int align, const char **ptrp)
{
	if (!flash_range_ok(offset, size_req, align))
		return -1;  /* Invalid range */
	if (ptrp)
		*ptrp = flash_physical_dataptr(offset);

	return CONFIG_FLASH_SIZE - offset;
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
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

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
 * Read and return persistent serial number.
 */
static const char *flash_read_pstate_serial(void)
{
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	if ((pstate->version == PERSIST_STATE_VERSION) &&
	    (pstate->valid_fields & PSTATE_VALID_SERIALNO)) {
		return (const char *)(pstate->serialno);
	}

	return 0;
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
	rv = flash_physical_erase(CONFIG_FW_PSTATE_OFF,
				  CONFIG_FW_PSTATE_SIZE);
	if (rv)
		return rv;

	/*
	 * Note that if we lose power in here, we'll lose the pstate contents.
	 * That's ok, because it's only possible to write the pstate before
	 * it's protected.
	 */

	/* Write the updated pstate */
	return flash_physical_write(CONFIG_FW_PSTATE_OFF, sizeof(*newpstate),
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
		pstate->valid_fields = 0;
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
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

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

/**
 * Write persistent serial number to pstate, erasing if necessary.
 *
 * @param serialno		New iascii serial number to set in pstate.
 * @return EC_SUCCESS, or nonzero if error.
 */
static int flash_write_pstate_serial(const char *serialno)
{
	int i;
	struct persist_state newpstate;
	const struct persist_state *pstate =
		(const struct persist_state *)
		flash_physical_dataptr(CONFIG_FW_PSTATE_OFF);

	/* Check that this is OK */
	if (!serialno)
		return EC_ERROR_INVAL;

	/* Cache the old copy for read/modify/write. */
	memcpy(&newpstate, pstate, sizeof(newpstate));
	validate_pstate_struct(&newpstate);

	/* Copy in serialno. */
	for (i = 0; i < SERIALNO_MAX - 1; i++) {
		newpstate.serialno[i] = serialno[i];
		if (serialno[i] == 0)
			break;
	}
	for (; i < SERIALNO_MAX; i++)
		newpstate.serialno[i] = 0;
	newpstate.valid_fields |= PSTATE_VALID_SERIALNO;

	return flash_write_pstate_data(&newpstate);
}




#else /* !CONFIG_FLASH_PSTATE_BANK */

/**
 * Return the address of the pstate data in EC-RO.
 */
static const uintptr_t get_pstate_addr(void)
{
	uintptr_t addr = (uintptr_t)&pstate_data;

	/* Always use the pstate data in RO, even if we're RW */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
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
	return flash_physical_write(get_pstate_addr() -
				    CONFIG_PROGRAM_MEMORY_BASE,
				    sizeof(new_pstate),
				    (const char *)&new_pstate);
}

#endif /* !CONFIG_FLASH_PSTATE_BANK */
#endif /* CONFIG_FLASH_PSTATE */

int flash_is_erased(uint32_t offset, int size)
{
	const uint32_t *ptr;

#ifdef CONFIG_MAPPED_STORAGE
	/* Use pointer directly to flash */
	if (flash_dataptr(offset, size, sizeof(uint32_t),
			  (const char **)&ptr) < 0)
		return 0;

	flash_lock_mapped_storage(1);
	for (size /= sizeof(uint32_t); size > 0; size--, ptr++)
		if (*ptr != CONFIG_FLASH_ERASED_VALUE32) {
			flash_lock_mapped_storage(0);
			return 0;
	}

	flash_lock_mapped_storage(0);
#else
	/* Read flash a chunk at a time */
	uint32_t buf[8];
	int bsize;

	while (size) {
		bsize = MIN(size, sizeof(buf));

		if (flash_read(offset, bsize, (char *)buf))
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

int flash_read(int offset, int size, char *data)
{
#ifdef CONFIG_MAPPED_STORAGE
	const char *src;

	if (flash_dataptr(offset, size, 1, &src) < 0)
		return EC_ERROR_INVAL;

	flash_lock_mapped_storage(1);
	memcpy(data, src, size);
	flash_lock_mapped_storage(0);
	return EC_SUCCESS;
#else
	return flash_physical_read(offset, size, data);
#endif
}

int flash_write(int offset, int size, const char *data)
{
	if (!flash_range_ok(offset, size, CONFIG_FLASH_WRITE_SIZE))
		return EC_ERROR_INVAL;  /* Invalid range */

#ifdef CONFIG_VBOOT_HASH
	/*
	* Abort hash calculations when flashrom flash updates
	* are in progress.Otherwise invalidate the pre-computed hash,
	* since it's likely to change after flash write
	*/
	if (vboot_hash_in_progress())
		vboot_hash_abort();
	else
		vboot_hash_invalidate(offset, size);
#endif

	return flash_physical_write(offset, size, data);
}

int flash_erase(int offset, int size)
{
	if (!flash_range_ok(offset, size, CONFIG_FLASH_ERASE_SIZE))
		return EC_ERROR_INVAL;  /* Invalid range */

#ifdef CONFIG_VBOOT_HASH
	/*
	* Abort hash calculations when flashrom flash updates
	* are in progress.Otherwise invalidate the pre-computed hash,
	* since it's likely to be wrong after erase.
	*/
	if (vboot_hash_in_progress())
		vboot_hash_abort();
	else
		vboot_hash_invalidate(offset, size);
#endif

	return flash_physical_erase(offset, size);
}

const char *flash_read_serial(void)
{
#if defined(CONFIG_FLASH_PSTATE) && defined(CONFIG_FLASH_PSTATE_BANK)
	return flash_read_pstate_serial();
#else
	return 0;
#endif
}

int flash_write_serial(const char *serialno)
{
#if defined(CONFIG_FLASH_PSTATE) && defined(CONFIG_FLASH_PSTATE_BANK)
	return flash_write_pstate_serial(serialno);
#else
	return EC_ERROR_UNIMPLEMENTED;
#endif
}

int flash_protect_at_boot(enum flash_wp_range range)
{
#ifdef CONFIG_FLASH_PSTATE
	uint32_t new_flags =
		(range != FLASH_WP_NONE) ? EC_FLASH_PROTECT_RO_AT_BOOT : 0;

	/* Read the current persist state from flash */
	if (flash_read_pstate() != new_flags) {
		/* Need to update pstate */
		int rv;

#ifdef CONFIG_FLASH_PSTATE_BANK
		/* Fail if write protect block is already locked */
		if (flash_physical_get_protect(PSTATE_BANK))
			return EC_ERROR_ACCESS_DENIED;
#endif

		/* Write the desired flags */
		rv = flash_write_pstate(new_flags);
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
	flash_physical_protect_at_boot(range);
#endif

	return EC_SUCCESS;
#else
	return flash_physical_protect_at_boot(range);
#endif
}

uint32_t flash_get_protect(void)
{
	uint32_t flags = 0;
	int not_protected[2] = {0};
	int i;

	/* Read write protect GPIO */
#ifdef CONFIG_WP_ALWAYS
	flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#elif defined(CONFIG_WP_ACTIVE_HIGH)
	if (gpio_get_level(GPIO_WP))
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#else
	if (!gpio_get_level(GPIO_WP_L))
		flags |= EC_FLASH_PROTECT_GPIO_ASSERTED;
#endif

#ifdef CONFIG_FLASH_PSTATE
	/* Read persistent state of RO-at-boot flag */
	flags |= flash_read_pstate();
#endif

	/* Scan flash protection */
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		/* Is this bank part of RO */
		int is_ro = (i >= WP_BANK_OFFSET &&
			     i < WP_BANK_OFFSET + WP_BANK_COUNT) ? 1 : 0;

		int bank_flag = (is_ro ? EC_FLASH_PROTECT_RO_NOW :
				EC_FLASH_PROTECT_ALL_NOW);

		if (flash_physical_get_protect(i)) {
			/* At least one bank in the region is protected */
			flags |= bank_flag;
			if (not_protected[is_ro])
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		} else {
			/* At least one bank in the region is NOT protected */
			not_protected[is_ro] = 1;
			if (flags & bank_flag)
				flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;
		}
	}

	/*
	 * If the RW banks are protected but the RO banks aren't, that's
	 * inconsistent.
	 *
	 * Note that we check this before adding in the physical flags below,
	 * since some chips can also protect ALL_NOW for the current boot by
	 * locking up the flash program-erase registers.
	 */
	if ((flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    !(flags & EC_FLASH_PROTECT_RO_NOW))
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/* Add in flags from physical layer */
	return flags | flash_physical_get_protect_flags();
}

int flash_set_protect(uint32_t mask, uint32_t flags)
{
	int retval = EC_SUCCESS;
	int rv;
	enum flash_wp_range range = FLASH_WP_NONE;
	int need_set_protect = 0;

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
	 *   1. RO_AT_BOOT was already set before flash_set_protect() is
	 *      called.
	 *   2. RO_AT_BOOT was not set, but it's requested to be set by
	 *      the caller of flash_set_protect().
	 */
	if (mask & EC_FLASH_PROTECT_RO_AT_BOOT) {
		range = (flags & EC_FLASH_PROTECT_RO_AT_BOOT) ?
			FLASH_WP_RO : FLASH_WP_NONE;
		need_set_protect = 1;
	}
	if ((mask & EC_FLASH_PROTECT_ALL_AT_BOOT) &&
	    !(flags & EC_FLASH_PROTECT_ALL_AT_BOOT)) {
		if (flash_get_protect() & EC_FLASH_PROTECT_RO_AT_BOOT)
			range = FLASH_WP_RO;
		need_set_protect = 1;
	}
	if (need_set_protect) {
		rv = flash_protect_at_boot(range);
		if (rv)
			retval = rv;
	}

	/*
	 * All subsequent flags only work if write protect is enabled (that is,
	 * hardware WP flag) *and* RO is protected at boot (software WP flag).
	 */
	if ((~flash_get_protect()) & (EC_FLASH_PROTECT_GPIO_ASSERTED |
				      EC_FLASH_PROTECT_RO_AT_BOOT))
		return retval;

	if ((mask & EC_FLASH_PROTECT_ALL_AT_BOOT) &&
	    (flags & EC_FLASH_PROTECT_ALL_AT_BOOT)) {
		rv = flash_protect_at_boot(FLASH_WP_ALL);
		if (rv)
			retval = rv;
	}

	if ((mask & EC_FLASH_PROTECT_RO_NOW) &&
	    (flags & EC_FLASH_PROTECT_RO_NOW)) {
		rv = flash_physical_protect_now(0);
		if (rv)
			retval = rv;
	}

	if ((mask & EC_FLASH_PROTECT_ALL_NOW) &&
	    (flags & EC_FLASH_PROTECT_ALL_NOW)) {
		rv = flash_physical_protect_now(1);
		if (rv)
			retval = rv;
	}

	return retval;
}

/*****************************************************************************/
/* Console commands */

static int command_flash_info(int argc, char **argv)
{
	int i;

	ccprintf("Usable:  %4d KB\n", CONFIG_FLASH_SIZE / 1024);
	ccprintf("Write:   %4d B (ideal %d B)\n", CONFIG_FLASH_WRITE_SIZE,
		 CONFIG_FLASH_WRITE_IDEAL_SIZE);
	ccprintf("Erase:   %4d B (to %d-bits)\n", CONFIG_FLASH_ERASE_SIZE,
		 CONFIG_FLASH_ERASED_VALUE32 ? 1 : 0);
	ccprintf("Protect: %4d B\n", CONFIG_FLASH_BANK_SIZE);

	i = flash_get_protect();
	ccprintf("Flags:  ");
	if (i & EC_FLASH_PROTECT_GPIO_ASSERTED)
		ccputs(" wp_gpio_asserted");
	if (i & EC_FLASH_PROTECT_RO_AT_BOOT)
		ccputs(" ro_at_boot");
	if (i & EC_FLASH_PROTECT_ALL_AT_BOOT)
		ccputs(" all_at_boot");
	if (i & EC_FLASH_PROTECT_RO_NOW)
		ccputs(" ro_now");
	if (i & EC_FLASH_PROTECT_ALL_NOW)
		ccputs(" all_now");
	if (i & EC_FLASH_PROTECT_ERROR_STUCK)
		ccputs(" STUCK");
	if (i & EC_FLASH_PROTECT_ERROR_INCONSISTENT)
		ccputs(" INCONSISTENT");
	ccputs("\n");

	ccputs("Protected now:");
	for (i = 0; i < CONFIG_FLASH_SIZE / CONFIG_FLASH_BANK_SIZE;
	     i++) {
		if (!(i & 31))
			ccputs("\n    ");
		else if (!(i & 7))
			ccputs(" ");
		ccputs(flash_physical_get_protect(i) ? "Y" : ".");
	}
	ccputs("\n");
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(flashinfo, command_flash_info,
			     NULL,
			     "Print flash info");

#ifdef CONFIG_CMD_FLASH
static int command_flash_erase(int argc, char **argv)
{
	int offset = -1;
	int size = CONFIG_FLASH_ERASE_SIZE;
	int rv;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_ERROR_ACCESS_DENIED;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x...\n", size, offset, offset);
	return flash_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase,
			"offset [size]",
			"Erase flash");

static int command_flash_write(int argc, char **argv)
{
	int offset = -1;
	int size = CONFIG_FLASH_ERASE_SIZE;
	int rv;
	char *data;
	int i;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
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

	ccprintf("Writing %d bytes to 0x%x...\n",
		 size, offset, offset);
	rv = flash_write(offset, size, data);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write,
			"offset [size]",
			"Write pattern to flash");

static int command_flash_read(int argc, char **argv)
{
	int offset = -1;
	int size = 256;
	int rv;
	char *data;
	int i;

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

	/* Read the data */
	if (flash_read(offset, size, data)) {
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
DECLARE_CONSOLE_COMMAND(flashread, command_flash_read,
			"offset [size]",
			"Read flash");
#endif

static int command_flash_wp(int argc, char **argv)
{
	int val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "now"))
		return flash_set_protect(EC_FLASH_PROTECT_ALL_NOW, -1);

	if (!strcasecmp(argv[1], "rw"))
		return flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, -1);

	if (!strcasecmp(argv[1], "norw"))
		return flash_set_protect(EC_FLASH_PROTECT_ALL_AT_BOOT, 0);

	/* Do this last, since anything starting with 'n' means "no" */
	if (parse_bool(argv[1], &val))
		return flash_set_protect(EC_FLASH_PROTECT_RO_AT_BOOT,
					 val ? -1 : 0);

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp,
			"<BOOLEAN> | now | rw | norw",
			"Modify flash write protect");

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
#define EC_FLASH_REGION_START MIN(CONFIG_EC_PROTECTED_STORAGE_OFF, \
				  CONFIG_EC_WRITABLE_STORAGE_OFF)

static int flash_command_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_flash_info_1 *r = args->response;

	r->flash_size = CONFIG_FLASH_SIZE - EC_FLASH_REGION_START;
	r->write_block_size = CONFIG_FLASH_WRITE_SIZE;
	r->erase_block_size = CONFIG_FLASH_ERASE_SIZE;
	r->protect_block_size = CONFIG_FLASH_BANK_SIZE;

	if (args->version == 0) {
		/* Only version 0 fields returned */
		args->response_size = sizeof(struct ec_response_flash_info);
	} else {
		/* Fill in full version 1 struct */

		/*
		 * Compute the ideal amount of data for the host to send us,
		 * based on the maximum response size and the ideal write size.
		 */
		r->write_ideal_size =
			(args->response_max -
			 sizeof(struct ec_params_flash_write)) &
			~(CONFIG_FLASH_WRITE_IDEAL_SIZE - 1);
		/*
		 * If we can't get at least one ideal block, then just want
		 * as high a multiple of the minimum write size as possible.
		 */
		if (!r->write_ideal_size)
			r->write_ideal_size =
				(args->response_max -
				 sizeof(struct ec_params_flash_write)) &
				~(CONFIG_FLASH_WRITE_SIZE - 1);

		r->flags = 0;

#if (CONFIG_FLASH_ERASED_VALUE32 == 0)
		r->flags |= EC_FLASH_INFO_ERASE_TO_0;
#endif

		args->response_size = sizeof(*r);
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_INFO,
		     flash_command_get_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int flash_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_read *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (p->size > args->response_max)
		return EC_RES_OVERFLOW;

	if (flash_read(offset, p->size, args->response))
		return EC_RES_ERROR;

	args->response_size = p->size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_READ,
		     flash_command_read,
		     EC_VER_MASK(0));

/**
 * Flash write command
 *
 * Version 0 and 1 are equivalent from the EC-side; the only difference is
 * that the host can only send 64 bytes of data at a time in version 0.
 */
static int flash_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_write *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_RES_ACCESS_DENIED;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

	if (system_unsafe_to_overwrite(offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_write(offset, p->size, (const uint8_t *)(p + 1)))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_WRITE,
		     flash_command_write,
		     EC_VER_MASK(0) | EC_VER_MASK(EC_VER_FLASH_WRITE));

/*
 * Make sure our image sizes are a multiple of flash block erase size so that
 * the host can erase the entire image.
 */
BUILD_ASSERT(CONFIG_RO_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);
BUILD_ASSERT(CONFIG_RW_SIZE % CONFIG_FLASH_ERASE_SIZE == 0);

static int flash_command_erase(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_erase *p = args->params;
	uint32_t offset = p->offset + EC_FLASH_REGION_START;

	if (flash_get_protect() & EC_FLASH_PROTECT_ALL_NOW)
		return EC_RES_ACCESS_DENIED;

	if (system_unsafe_to_overwrite(offset, p->size))
		return EC_RES_ACCESS_DENIED;

	/* Indicate that we might be a while */
#if defined(HAS_TASK_HOSTCMD) && defined(CONFIG_HOST_COMMAND_STATUS)
	args->result = EC_RES_IN_PROGRESS;
	host_send_response(args);
#endif
	if (flash_erase(offset, p->size))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_ERASE,
		     flash_command_erase,
		     EC_VER_MASK(0));

static int flash_command_protect(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_protect *p = args->params;
	struct ec_response_flash_protect *r = args->response;

	/*
	 * Handle requesting new flags.  Note that we ignore the return code
	 * from flash_set_protect(), since errors will be visible to the caller
	 * via the flags in the response.  (If we returned error, the caller
	 * wouldn't get the response.)
	 */
	if (p->mask)
		flash_set_protect(p->mask, p->flags);

	/*
	 * Retrieve the current flags.  The caller can use this to determine
	 * which of the requested flags could be set.  This is cleaner than
	 * simply returning error, because it provides information to the
	 * caller about the actual result.
	 */
	r->flags = flash_get_protect();

	/* Indicate which flags are valid on this platform */
	r->valid_flags =
		EC_FLASH_PROTECT_GPIO_ASSERTED |
		EC_FLASH_PROTECT_ERROR_STUCK |
		EC_FLASH_PROTECT_ERROR_INCONSISTENT |
		flash_physical_get_valid_flags();
	r->writable_flags = flash_physical_get_writable_flags(r->flags);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

/*
 * TODO(crbug.com/239197) : Adding both versions to the version mask is a
 * temporary workaround for a problem in the cros_ec driver. Drop
 * EC_VER_MASK(0) once cros_ec driver can send the correct version.
 */
DECLARE_HOST_COMMAND(EC_CMD_FLASH_PROTECT,
		     flash_command_protect,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int flash_command_region_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_region_info *p = args->params;
	struct ec_response_flash_region_info *r = args->response;

	switch (p->region) {
	case EC_FLASH_REGION_RO:
		r->offset = CONFIG_EC_PROTECTED_STORAGE_OFF +
			    CONFIG_RO_STORAGE_OFF -
			    EC_FLASH_REGION_START;
		r->size = CONFIG_RO_SIZE;
		break;
	case EC_FLASH_REGION_RW:
		r->offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
			    CONFIG_RW_STORAGE_OFF -
			    EC_FLASH_REGION_START;
		r->size = CONFIG_RW_SIZE;
		break;
	case EC_FLASH_REGION_WP_RO:
		r->offset = CONFIG_WP_STORAGE_OFF -
			    EC_FLASH_REGION_START;
		r->size = CONFIG_WP_STORAGE_SIZE;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_REGION_INFO,
		     flash_command_region_info,
		     EC_VER_MASK(EC_VER_FLASH_REGION_INFO));
