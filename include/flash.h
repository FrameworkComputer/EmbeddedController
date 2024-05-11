/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#ifndef __CROS_EC_FLASH_H
#define __CROS_EC_FLASH_H

#include "common.h"
#include "ec_commands.h" /* For EC_FLASH_PROTECT_* flags */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_FLASH_MULTIPLE_REGION
#ifndef CONFIG_ZEPHYR
extern struct ec_flash_bank const
	flash_bank_array[CONFIG_FLASH_REGION_TYPE_COUNT];
#endif

/*
 * Return the bank the offset is in.
 * Return -1 if the offset is not at the beginning of that bank.
 */
int crec_flash_bank_index(int offset);

/*
 * Number of banks between offset and offset+size.
 *
 * offset and offset + size should be addresses at the beginning of bank:
 * 0                   32
 * +-------------------+--------...
 * |  bank 0           | bank 1 ...
 * +-------------------+--------...
 * In that case, begin = 0, end = 1, return is 1.
 * otherwise, this is an error:
 * 0          32       64
 * +----------+--------+--------...
 * |  bank 0           | bank 1 ...
 * +----------+--------+--------...
 * begin = 0, end = -1....
 * The idea is to prevent erasing more than you think.
 */
int crec_flash_bank_count(int offset, int size);

/*
 * Return the size of the specified bank in bytes.
 * Return -1 if the bank is too large.
 */
int crec_flash_bank_size(int bank);

int crec_flash_bank_start_offset(int bank);

int crec_flash_bank_erase_size(int bank);

void crec_flash_print_region_info(void);

/* Number of physical flash banks */
#define PHYSICAL_BANKS CONFIG_FLASH_MULTIPLE_REGION

/* WP region offset and size in units of flash banks */
#define WP_BANK_OFFSET crec_flash_bank_index(CONFIG_WP_STORAGE_OFF)
#define WP_BANK_COUNT \
	(crec_flash_bank_count(CONFIG_WP_STORAGE_OFF, CONFIG_WP_STORAGE_SIZE))

#else /* CONFIG_FLASH_MULTIPLE_REGION */
/* Number of physical flash banks */
#ifndef PHYSICAL_BANKS
#define PHYSICAL_BANKS (CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE)
#endif

/* WP region offset and size in units of flash banks */
#define WP_BANK_OFFSET (CONFIG_WP_STORAGE_OFF / CONFIG_FLASH_BANK_SIZE)
#ifndef WP_BANK_COUNT
#define WP_BANK_COUNT (CONFIG_WP_STORAGE_SIZE / CONFIG_FLASH_BANK_SIZE)
#endif
#endif /* CONFIG_FLASH_MULTIPLE_REGION */

/**
 * Get number of flash banks
 *
 * @return number of flash banks
 */
int crec_flash_total_banks(void);

/**
 * Fill flash info response structure (version 2)
 *
 * The function is responsible for filling 'num_banks_desc', 'num_banks_total'
 * and 'banks' fields with information about flash layout.
 *
 * We are passing the whole response structure because it is marked
 * as '__ec_align4', so it's packed, and should be aligned also but on most
 * systems it's not because CONFIG_HOSTCMD_OPTION is not enabled. It means that
 * the structure can be placed at ANY address. Passing the response structure
 * gives information to the compiler how members should be accessed.
 * Passing pointer to structure member is an error, and compiler will warn
 * about it. Taking pointer to structure member, passing it as uint8_t and
 * casting it is dangerous because the compiler will assume that the address
 * is aligned and you won't get any warning about it.
 *
 * @param pointer to flash info version 2 response structure
 * @param size of 'banks' array inside response structure
 * @return EC_RES_SUCCESS or other error code.
 */
int crec_flash_response_fill_banks(struct ec_response_flash_info_2 *r,
				   int num_banks);

/* Persistent protection state flash offset / size / bank */
#if defined(CONFIG_FLASH_PSTATE) && defined(CONFIG_FLASH_PSTATE_BANK)

#ifdef CONFIG_FLASH_MULTIPLE_REGION
#error "Not supported."
#endif

/*
 * When there is a dedicated flash bank used to store persistent state,
 * ensure the RO flash region excludes the PSTATE bank.
 */
#define EC_FLASH_REGION_RO_SIZE CONFIG_RO_SIZE

#ifndef PSTATE_BANK
#define PSTATE_BANK (CONFIG_FW_PSTATE_OFF / CONFIG_FLASH_BANK_SIZE)
#endif
#ifndef PSTATE_BANK_COUNT
#define PSTATE_BANK_COUNT (CONFIG_FW_PSTATE_SIZE / CONFIG_FLASH_BANK_SIZE)
#endif
#else /* CONFIG_FLASH_PSTATE && CONFIG_FLASH_PSTATE_BANK */
/* Allow flashrom to program the entire write protected area */
#define EC_FLASH_REGION_RO_SIZE CONFIG_WP_STORAGE_SIZE
#define PSTATE_BANK_COUNT 0
#endif /* CONFIG_FLASH_PSTATE && CONFIG_FLASH_PSTATE_BANK */

#ifdef CONFIG_ROLLBACK
/*
 * ROLLBACK region offset and size in units of flash banks.
 */
#ifdef CONFIG_FLASH_MULTIPLE_REGION
#define ROLLBACK_BANK_OFFSET crec_flash_bank_index(CONFIG_ROLLBACK_OFF)
#define ROLLBACK_BANK_COUNT \
	crec_flash_bank_count(CONFIG_ROLLBACK_OFF, CONFIG_ROLLBACK_SIZE)
#else
#define ROLLBACK_BANK_OFFSET (CONFIG_ROLLBACK_OFF / CONFIG_FLASH_BANK_SIZE)
#define ROLLBACK_BANK_COUNT (CONFIG_ROLLBACK_SIZE / CONFIG_FLASH_BANK_SIZE)
#endif /* CONFIG_FLASH_MULTIPLE_REGION */
#endif /* CONFIG_ROLLBACK */

#ifdef CONFIG_FLASH_PROTECT_RW
#ifdef CONFIG_FLASH_MULTIPLE_REGION
#define RW_BANK_OFFSET crec_flash_bank_index(CONFIG_EC_WRITABLE_STORAGE_OFF)
#define RW_BANK_COUNT                                         \
	crec_flash_bank_count(CONFIG_EC_WRITABLE_STORAGE_OFF, \
			      CONFIG_EC_WRITABLE_STORAGE_SIZE)
#else
#define RW_BANK_OFFSET (CONFIG_EC_WRITABLE_STORAGE_OFF / CONFIG_FLASH_BANK_SIZE)
#define RW_BANK_COUNT (CONFIG_EC_WRITABLE_STORAGE_SIZE / CONFIG_FLASH_BANK_SIZE)
#endif

#endif /* CONFIG_FLASH_PROTECT_RW */

/* This enum is useful to identify different regions during verification. */
enum flash_region {
	FLASH_REGION_RW = 0,
	FLASH_REGION_RO,
#ifdef CONFIG_ROLLBACK
	FLASH_REGION_ROLLBACK,
#endif
	FLASH_REGION_COUNT
};

/*****************************************************************************/
/* Low-level methods, for use by flash_common. */

/**
 * Read from physical flash.
 *
 * @param offset	Flash offset to write.
 * @param size	        Number of bytes to write.
 * @param data          Destination buffer for data.  Must be 32-bit aligned.
 */
int crec_flash_physical_read(int offset, int size, char *data);

/**
 * Write to physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param offset	Flash offset to write.
 * @param size	        Number of bytes to write.
 * @param data          Data to write to flash.  Must be 32-bit aligned.
 */
int crec_flash_physical_write(int offset, int size, const char *data);

/**
 * Erase physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param offset	Flash offset to erase.
 * @param size	        Number of bytes to erase.
 */
int crec_flash_physical_erase(int offset, int size);

/**
 * Read physical write protect setting for a flash bank.
 *
 * @param bank	        Bank index to check.
 * @return non-zero if bank is protected until reboot.
 */
int crec_flash_physical_get_protect(int bank);

/**
 * Return flash protect state flags from the physical layer.
 *
 * This should only be called by flash_get_protect().
 *
 * Uses the EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_protect_flags(void);

/**
 * Enable/disable protecting firmware/pstate at boot.
 *
 * @param new_flags to protect (only EC_FLASH_PROTECT_*_AT_BOOT are
 * taken care of)
 * @return non-zero if error.
 */
int crec_flash_physical_protect_at_boot(uint32_t new_flags);

/**
 * Protect flash now.
 *
 * @param all		Protect all (=1) or just read-only and pstate (=0).
 * @return non-zero if error.
 */
int crec_flash_physical_protect_now(int all);

/**
 * Force reload of flash protection bits.
 *
 * Some EC architectures (STM32L) only load the bits from option bytes at
 * power-on reset or via a special command.  This issues that command if
 * possible, which triggers a power-on reboot.
 *
 * Only returns (with EC_ERROR_ACCESS_DENIED) if the command is locked.
 */
int crec_flash_physical_force_reload(void);

/**
 * Restore flash physical layer state after sysjump.
 *
 * @return non-zero if restored.
 */
int crec_flash_physical_restore_state(void);

/**
 * Return the valid flash protect flags.
 *
 * @return a combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_valid_flags(void);

/**
 * Return the writable flash protect flags.
 *
 * @param cur_flags The current flash protect flags.
 * @return a combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags);

/*****************************************************************************/
/* Low-level common code for use by flash modules. */

/**
 * Check if a region of flash is erased
 *
 * It is assumed that an erased region has all bits set to 1.
 *
 * @param offset	Flash offset to check
 * @param size		Number of bytes to check (word-aligned)
 * @return 1 if erased, 0 if not erased
 */
int crec_flash_is_erased(uint32_t offset, int size);

/**
 * Enable write protect for the specified range.
 *
 * Once write protect is enabled, it will STAY enabled until the system is
 * hard-rebooted with the hardware write protect pin deasserted.  If the write
 * protect pin is deasserted, the protect setting is ignored, and the entire
 * flash will be writable.
 *
 * @param new_flags to protect (only EC_FLASH_PROTECT_*_AT_BOOT are
 * taken care of)
 * @return EC_SUCCESS, or nonzero if error.
 */
int crec_flash_protect_at_boot(uint32_t new_flags);

/*****************************************************************************/
/* High-level interface for use by other modules. */

/**
 * Initialize the module.
 *
 * Applies at-boot protection settings if necessary.
 */
int crec_flash_pre_init(void);

/**
 * Return the usable size of flash in bytes.  Note that this may be smaller
 * than the physical flash size.
 */
int crec_flash_get_size(void);

/**
 * Get the physical memory address of a flash offset
 *
 * This is used for direct flash access. We assume that the flash is
 * contiguous from this start address through to the end of the usable
 * flash.
 *
 * This function returns -1 if offset + size_req extends beyond the end
 * of flash, the offset is out of range, or if either size_req or offset
 * are not aligned to 'align'.
 *
 * @param offset	Flash offset to get address of
 * @param size_req	Number of bytes requested
 * @param align		Ensure offset and size_req are aligned to given
 *			power of two.
 * @param ptrp		If not NULL, returns a pointer to this flash offset
 *			in memory, unless function fails, iwc it is unset.
 * @return size of flash region available at *ptrp, or -1 on error
 */
int crec_flash_dataptr(int offset, int size_req, int align, const char **ptrp);

/**
 * Read from flash without hiding protected sections data
 *
 * If flash is mapped (CONFIG_MAPPED_STORAGE), it is usually more efficient to
 * use flash_dataptr() to get a pointer directly to the flash memory rather
 * than use flash_read(), since the former saves a memcpy() operation.
 *
 * This method won't hide the protected flash sections data.
 *
 * @param offset	Flash offset to read.
 * @param size		Number of bytes to read.
 * @param data		Destination buffer for data.  Must be 32-bit aligned.
 */
int crec_flash_unprotected_read(int offset, int size, char *data);

/**
 * Read from flash.
 *
 * If flash is mapped (CONFIG_MAPPED_STORAGE), it is usually more efficient to
 * use flash_dataptr() to get a pointer directly to the flash memory rather
 * than use flash_read(), since the former saves a memcpy() operation.
 *
 * This method hides the protected flash sections data.
 *
 * @param offset	Flash offset to read.
 * @param size		Number of bytes to read.
 * @param data		Destination buffer for data.  Must be 32-bit aligned.
 */
int crec_flash_read(int offset, int size, char *data);

/**
 * Write to flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param offset	Flash offset to write.
 * @param size	        Number of bytes to write.
 * @param data          Data to write to flash.  Must be 32-bit aligned.
 */
int crec_flash_write(int offset, int size, const char *data);

/**
 * Erase flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param offset	Flash offset to erase.
 * @param size	        Number of bytes to erase.
 */
int crec_flash_erase(int offset, int size);

/**
 * Return the flash protect state.
 *
 * Uses the EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_get_protect(void);

/**
 * Set the flash protect state.
 *
 * @param mask		Bits in flags to apply.
 * @param flags		New values for flags.
 */
int crec_flash_set_protect(uint32_t mask, uint32_t flags);

/**
 * Get the serial number from flash.
 *
 * @return char * ascii serial number string.
 *     NULL if error.
 */
const char *crec_flash_read_pstate_serial(void);

/**
 * Set the serial number in flash.
 *
 * @param serialno	ascii serial number string.
 *
 * @return success status.
 */
int crec_flash_write_pstate_serial(const char *serialno);

/**
 * Get the MAC address from flash.
 *
 * @return char * ascii MAC address string.
 *     Format: "01:23:45:67:89:AB"
 *     NULL if error.
 */
const char *crec_flash_read_pstate_mac_addr(void);

/**
 * Set the MAC address in flash.
 *
 * @param mac_addr	ascii MAC address string.
 *     Format: "01:23:45:67:89:AB"
 *
 * @return success status.
 */
int crec_flash_write_pstate_mac_addr(const char *mac_addr);

/**
 * Get the poweron config from flash.
 *
 *  @param poweron_conf	pointer to uint8_t buffer,
 *         which need to be in size of CONFIG_POWERON_CONF_LEN
 *
 * @return success status
 */
int crec_flash_read_pstate_poweron_conf(uint8_t *poweron_conf);

/**
 * Set the poweron config in flash.
 *
 * @param poweron_conf pointer to uint8_t buffer,
 *        which need to be in size of CONFIG_POWERON_CONF_LEN
 *
 * @return success status.
 */
int crec_flash_write_pstate_poweron_conf(const uint8_t *poweron_conf);

#ifdef CONFIG_FLASH_EX_OP_ENABLED
/**
 * Flash device register's reset.
 *
 */
void crec_flash_reset(void);
#endif

/**
 * Lock or unlock HW necessary for mapped storage read.
 *
 * @param lock          1 to lock, 0 to unlock.
 */
#ifdef CONFIG_EXTERNAL_STORAGE
void crec_flash_lock_mapped_storage(int lock);
#else
static inline void crec_flash_lock_mapped_storage(int lock){};
#endif /* CONFIG_EXTERNAL_STORAGE */

/**
 * Select flash for performing flash operations. Board should implement this
 * if some steps needed be done before flash operation can succeed.
 *
 * @param select   1 to select flash, 0 to deselect (disable).
 * @return EC_RES_* status code.
 */
int crec_board_flash_select(int select);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FLASH_H */
