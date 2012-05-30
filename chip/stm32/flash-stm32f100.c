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

#define FLASH_WRITE_BYTES     64
#define FLASH_ERASE_BYTES   1024
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
#define KEY1    0x45670123
#define KEY2    0xCDEF89AB

/* Lock bits*/
#define CR_LOCK  (1<<7)
#define PRG_LOCK 0
#define OPT_LOCK (1<<9)

int flash_get_write_block_size(void)
{
	return FLASH_WRITE_BYTES;
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
	/* unlock CR if needed */
	if (STM32_FLASH_CR & CR_LOCK) {
		STM32_FLASH_KEYR = KEY1;
		STM32_FLASH_KEYR = KEY2;
	}
	/* unlock option memory if required */
	if ((locks & OPT_LOCK) && !(STM32_FLASH_CR & OPT_LOCK)) {
		STM32_FLASH_OPTKEYR = KEY1;
		STM32_FLASH_OPTKEYR = KEY2;
	}

	return ((STM32_FLASH_CR ^ OPT_LOCK) & (locks | CR_LOCK)) ?
			EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static void lock(void)
{
	STM32_FLASH_CR = CR_LOCK;
}

static uint8_t read_optb(int byte)
{
	return *(uint8_t *)(STM32_OPTB_BASE + byte);

}

static void write_optb(int byte, uint8_t value)
{
	volatile int16_t *hword = (uint16_t *)(STM32_OPTB_BASE + byte);

	if (unlock(OPT_LOCK) != EC_SUCCESS)
		return;

	/* set OPTPG bit */
	STM32_FLASH_CR |= (1<<4);

	/*TODO: how do we manage erasing (aka OPTER) ? */

	*hword = value ;

	/* reset OPTPG bit */
	STM32_FLASH_CR |= (1<<4);

	lock();
}

int flash_physical_write(int offset, int size, const char *data)
{
	/* this is pretty nasty, we need to enforce alignment instead of this
	 * wild cast : TODO crosbug.com/p/9526
	 */
	uint16_t *data16 = (uint16_t *)data;
	uint16_t *address = (uint16_t *)(CONFIG_FLASH_BASE + offset);
	int res = EC_SUCCESS;
	int i;

	if (unlock(PRG_LOCK) != EC_SUCCESS) {
		res = EC_ERROR_UNKNOWN;
		goto exit_wr;
	}

	/* Clear previous error status */
	STM32_FLASH_SR = 0x34;

	/* set PG bit */
	STM32_FLASH_CR |= (1<<0);


	for ( ; size > 0; size -= sizeof(*data16)) {
#ifdef CONFIG_TASK_WATCHDOG
		/* Reload the watchdog timer to avoid watchdog reset when doing
		 * long writing with interrupt disabled.
		 */
		watchdog_reload();
#endif
		/* wait to be ready  */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP) ;
		     i++)
			;

		/* write the half word */
		*address++ = *data16++;

		/* Wait for writes to complete */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP) ;
		     i++)
			;

		if (STM32_FLASH_SR & 1) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}

		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & 0x14) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	/* Disable PG bit */
	STM32_FLASH_CR &= ~(1<<0);

	lock();

	return res;
}


int flash_physical_erase(int offset, int size)
{
	uint32_t address;
	int res = EC_SUCCESS;

	if (unlock(PRG_LOCK) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/* Clear previous error status */
	STM32_FLASH_SR = 0x34;

	/* set PER bit */
	STM32_FLASH_CR |= (1<<1);

	for (address = CONFIG_FLASH_BASE + offset ;
	     size > 0; size -= FLASH_ERASE_BYTES,
	     address += FLASH_ERASE_BYTES) {
		timestamp_t deadline;

		/* select page to erase */
		STM32_FLASH_AR = address;

		/* set STRT bit : start erase */
		STM32_FLASH_CR |= (1<<6);
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
		if (STM32_FLASH_SR & 0x14) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	/* reset PER bit */
	STM32_FLASH_CR &= ~(1<<1);

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
	if (0) { /* TODO: crosbug.com/p/9849 verify WP */
	int byte_off = STM32_OPTB_WRP_OFF(block/8);
	uint8_t val = read_optb(byte_off) | (1 << (block % 8));
	write_optb(byte_off, val);
	}
}
