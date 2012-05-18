/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "console.h"
#include "flash.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* crosbug.com/p/9811 workaround 64-byte payload limitation */
#define CONFIG_64B_WORKAROUND

#define FLASH_WRITE_BYTES    128
#define FLASH_ERASE_BYTES    256
#define FLASH_PROTECT_BYTES 4096

#define US_PER_SECOND 1000000

/* the approximate number of CPU cycles per iteration of the loop when polling
 * the flash status
 */
#define CYCLE_PER_FLASH_LOOP 10

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_TIMEOUT_US 16000
#define FLASH_TIMEOUT_LOOP \
	(FLASH_TIMEOUT_US * (CPU_CLOCK / US_PER_SECOND) / CYCLE_PER_FLASH_LOOP)

/* Flash unlocking keys */
#define PEKEY1  0x89ABCDEF
#define PEKEY2  0x02030405
#define PRGKEY1 0x8C9DAEBF
#define PRGKEY2 0x13141516
#define OPTKEY1 0xFBEAD9C8
#define OPTKEY2 0x24252627

/* Lock bits*/
#define PE_LOCK  (1<<0)
#define PRG_LOCK (1<<1)
#define OPT_LOCK (1<<2)

#ifdef CONFIG_64B_WORKAROUND
/* used to buffer the write payload smaller than the half page size */
static uint32_t write_buffer[FLASH_WRITE_BYTES / sizeof(uint32_t)];
static int buffered_off = -1;
#endif

int flash_get_write_block_size(void)
{
#ifdef CONFIG_64B_WORKAROUND
	return 64;
#else
	return FLASH_WRITE_BYTES;
#endif
}


int flash_get_erase_block_size(void)
{
	return FLASH_ERASE_BYTES;
}


int flash_get_protect_block_size(void)
{
	BUILD_ASSERT(FLASH_PROTECT_BYTES == CONFIG_FLASH_BANK_SIZE);
	return FLASH_PROTECT_BYTES;
}


int flash_physical_size(void)
{
	return CONFIG_FLASH_SIZE;
}


int flash_physical_read(int offset, int size, char *data)
{
	/* Just read the flash from its memory window. */
	/* TODO: (crosbug.com/p/7473) is this affected by data cache? */
	memcpy(data, (char *)offset, size);
	return EC_SUCCESS;
}

static int unlock(int locks)
{
	/* unlock PECR if needed */
	if (STM32_FLASH_PECR & PE_LOCK) {
		STM32_FLASH_PEKEYR = PEKEY1;
		STM32_FLASH_PEKEYR = PEKEY2;
	}
	/* unlock program memory if required */
	if ((locks & PRG_LOCK) && (STM32_FLASH_PECR & PRG_LOCK)) {
		STM32_FLASH_PRGKEYR = PRGKEY1;
		STM32_FLASH_PRGKEYR = PRGKEY2;
	}
	/* unlock option memory if required */
	if ((locks & OPT_LOCK) && (STM32_FLASH_PECR & 4)) {
		STM32_FLASH_OPTKEYR = OPTKEY1;
		STM32_FLASH_OPTKEYR = OPTKEY2;
	}

	return (STM32_FLASH_PECR & (locks | PE_LOCK)) ?
			EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static void lock(void)
{
	STM32_FLASH_PECR = 0x7;
}

static uint8_t read_optb(int byte)
{
	return *(uint8_t *)(STM32_OPTB_BASE + byte);

}

static void write_optb(int byte, uint8_t value)
{
	volatile int32_t *word = (uint32_t *)(STM32_OPTB_BASE + (byte & ~0x3));
	uint32_t val = *word;
	int shift = (byte & 0x3) * 8;

	if (unlock(OPT_LOCK) != EC_SUCCESS)
		return;

	val &= ~((0xff << shift) | (0xff << (shift + STM32_OPTB_COMPL_SHIFT)));
	val |= (value << shift) | (~value << (shift + STM32_OPTB_COMPL_SHIFT));
	*word = val;

	/* TODO reboot by writing OBL_LAUNCH bit ? */

	lock();
}

/**
 * This function lives in internal RAM,
 * as we cannot read flash during writing.
 * You should neither call other function from this one,
 * nor declare it static.
 */
void  __attribute__((section(".iram.text")))
	iram_flash_write(uint32_t *addr, uint32_t *data)
{
	int i;

	interrupt_disable();

	/* wait to be ready  */
	for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP) ;
	     i++)
		;

	/* set PROG and FPRG bits */
	STM32_FLASH_PECR |= (1<<3) | (1<<10);

	/* send words for the half page */
	for (i = 0; i < FLASH_WRITE_BYTES / sizeof(uint32_t); i++)
		*addr++ = *data++;

	/* Wait for writes to complete */
	for (i = 0; ((STM32_FLASH_SR & 9) != 8) && (i < FLASH_TIMEOUT_LOOP) ;
	     i++)
		;

	/* Disable PROG and FPRG bits */
	STM32_FLASH_PECR &= ~((1<<3) | (1<<10));

	interrupt_enable();
}

int flash_physical_write(int offset, int size, const char *data)
{
	/* this is pretty nasty, we need to enforce alignment instead of this
	 * wild cast : TODO crosbug.com/p/9526
	 */
	uint32_t *data32 = (uint32_t *)data;
	uint32_t *address;
	int res = EC_SUCCESS;

#ifdef CONFIG_64B_WORKAROUND
		if ((size < FLASH_WRITE_BYTES) || (offset & 64)) {
			if ((size != 64) ||
			    ((offset & 64) && (buffered_off != offset - 64))) {
				res = EC_ERROR_UNKNOWN;
				goto exit_wr;

			}
			if (offset & 64) {
				/* second 64B packet : flash ! */
				memcpy(write_buffer + 16, data32, 64);
				offset -= 64;
				size += 64;
				data32 = write_buffer;
			} else {
				/* first 64B packet : just store it */
				buffered_off = offset;
				memcpy(write_buffer, data32, 64);
				return EC_SUCCESS;
			}
		}
#endif

	if (unlock(PRG_LOCK) != EC_SUCCESS) {
		res = EC_ERROR_UNKNOWN;
		goto exit_wr;
	}

	/* Clear previous error status */
	STM32_FLASH_SR = 0xf00;

	for (address = (uint32_t *)(CONFIG_FLASH_BASE + offset) ;
	     size > 0; size -= FLASH_WRITE_BYTES) {
#ifdef CONFIG_TASK_WATCHDOG
		/* Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();
#endif
		iram_flash_write(address, data32);

		address += FLASH_WRITE_BYTES / sizeof(uint32_t);
		data32 += FLASH_WRITE_BYTES / sizeof(uint32_t);
		if (STM32_FLASH_SR & 1) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & 0xF00) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	lock();

	return res;
}


int flash_physical_erase(int offset, int size)
{
	uint32_t *address;
	int res = EC_SUCCESS;

	if (unlock(PRG_LOCK) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_SR = 0xf00;

	/* set PROG and ERASE bits */
	STM32_FLASH_PECR |= (1<<3) | (1<<9);

	for (address = (uint32_t *)(CONFIG_FLASH_BASE + offset) ;
	     size > 0; size -= FLASH_ERASE_BYTES,
	     address += FLASH_ERASE_BYTES/sizeof(uint32_t)) {
		timestamp_t deadline;

		/* Start erase */
		*address = 0x00000000;

#ifdef CONFIG_TASK_WATCHDOG
		/* Reload the watchdog timer in case the erase takes long time
		 * so that erasing many flash pages
		 */
		watchdog_reload();
#endif

		deadline.val = get_time().val + FLASH_TIMEOUT_US;
		/* Wait for erase to complete */
		while ((STM32_FLASH_SR & 1) &&
		       (get_time().val < deadline.val)) {
			usleep(300);
		}
		if (STM32_FLASH_SR & 1) {
			res = EC_ERROR_TIMEOUT;
			goto exit_er;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & 0xF00) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	lock();

	return res;
}


int flash_physical_get_protect(int block)
{
	uint8_t val = read_optb(STM32_OPTB_WRP_OFF(block/8));
	return val & (1 << (block % 8));
}


void flash_physical_set_protect(int block)
{
	int byte_off = STM32_OPTB_WRP_OFF(block/8);
	uint8_t val = read_optb(byte_off) | (1 << (block % 8));
	write_optb(byte_off, val);
}
