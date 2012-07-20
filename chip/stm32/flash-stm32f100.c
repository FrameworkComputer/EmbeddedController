/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "console.h"
#include "flash.h"
#include "registers.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

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

/* Lock bits for FLASH_CR register */
#define PG       (1<<0)
#define PER      (1<<1)
#define OPTPG    (1<<4)
#define OPTER    (1<<5)
#define STRT     (1<<6)
#define CR_LOCK  (1<<7)
#define PRG_LOCK 0
#define OPT_LOCK (1<<9)

/* Fake write protect switch for flash write protect development.
 * TODO: Remove this when we have real write protect pin. */
static int fake_write_protect;

static void write_optb(int byte, uint8_t value);

int flash_physical_size(void)
{
	return CONFIG_FLASH_SIZE;
}

static int wait_busy(void)
{
	int timeout = FLASH_TIMEOUT_LOOP;
	while (STM32_FLASH_SR & (1 << 0) && timeout-- > 0)
		udelay(CYCLE_PER_FLASH_LOOP);
	return (timeout > 0) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
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

/*
 * Option byte organization
 *
 *                 [31:24]    [23:16]    [15:8]   [7:0]
 *
 *   0x1FFF_F800    nUSER      USER       nRDP     RDP
 *
 *   0x1FFF_F804    nData1     Data1     nData0    Data0
 *
 *   0x1FFF_F808    nWRP1      WRP1      nWRP0     WRP0
 *
 *   0x1FFF_F80C    nWRP3      WRP2      nWRP2     WRP2
 *
 * Note that the variable with n prefix means the complement.
 */
static uint8_t read_optb(int byte)
{
	return *(uint8_t *)(STM32_OPTB_BASE + byte);
}

static void erase_optb(void)
{
	wait_busy();

	if (unlock(OPT_LOCK) != EC_SUCCESS)
		return;

	/* Must be set in 2 separate lines. */
	STM32_FLASH_CR |= OPTER;
	STM32_FLASH_CR |= STRT;

	wait_busy();
	lock();
}

/*
 * Since the option byte erase is WHOLE erase, this function is to keep
 * rest of bytes, but make this byte 0xff.
 * Note that this could make a recursive call to write_optb().
 */
static void preserve_optb(int byte)
{
	int i;
	uint8_t optb[8];

	/* The byte has been reset, no need to run preserve. */
	if (*(uint16_t *)(STM32_OPTB_BASE + byte) == 0xffff)
		return;

	for (i = 0; i < ARRAY_SIZE(optb); ++i)
		optb[i] = read_optb(i * 2);

	optb[byte / 2] = 0xff;

	erase_optb();
	for (i = 0; i < ARRAY_SIZE(optb); ++i)
		write_optb(i * 2, optb[i]);
}

static void write_optb(int byte, uint8_t value)
{
	volatile int16_t *hword = (uint16_t *)(STM32_OPTB_BASE + byte);

	wait_busy();

	/* The target byte is the value we want to write. */
	if (*(uint8_t *)hword == value)
		return;

	/* Try to erase that byte back to 0xff. */
	preserve_optb(byte);

	if (unlock(OPT_LOCK) != EC_SUCCESS)
		return;

	/* set OPTPG bit */
	STM32_FLASH_CR |= OPTPG;

	*hword = value ;

	/* reset OPTPG bit */
	STM32_FLASH_CR &= ~OPTPG;

	wait_busy();
	lock();
}

int flash_physical_write(int offset, int size, const char *data)
{
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
	STM32_FLASH_CR |= PG;


	for ( ; size > 0; size -= sizeof(uint16_t)) {
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
		*address++ = data[0] + (data[1] << 8);
		data += 2;

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
	STM32_FLASH_CR &= ~PG;

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
	STM32_FLASH_CR |= PER;

	for (address = CONFIG_FLASH_BASE + offset ;
	     size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
	     address += CONFIG_FLASH_ERASE_SIZE) {
		timestamp_t deadline;

		/* select page to erase */
		STM32_FLASH_AR = address;

		/* set STRT bit : start erase */
		STM32_FLASH_CR |= STRT;
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
	STM32_FLASH_CR &= ~PER;

	lock();

	return res;
}


int flash_physical_get_protect(int block)
{
	uint8_t val = read_optb(STM32_OPTB_WRP_OFF(block/8));
	return !(val & (1 << (block % 8)));
}

void flash_physical_set_protect(int start_bank, int bank_count)
{
	int block;
	int i;
	int original_val[8], val[8];

	for (i = 0; i < 8; ++i)
		original_val[i] = val[i] = read_optb(i * 2);

	for (block = start_bank; block < start_bank + bank_count; block++) {
		int byte_off = STM32_OPTB_WRP_OFF(block/8) / 2;
		val[byte_off] = val[byte_off] & (~(1 << (block % 8)));
	}

	for (i = 0; i < 8; ++i)
		if (original_val[i] != val[i])
			write_optb(i * 2, val[i]);
}

static void unprotect_all_blocks(void)
{
	int i;
	for (i = 4; i < 8; ++i)
		write_optb(i * 2, 0xff);
}

int flash_physical_pre_init(void)
{
	/* Drop write protect status here. If a block should be protected,
	 * write protect for it will be set by pstate. */
	unprotect_all_blocks();

	return EC_SUCCESS;
}

int write_protect_asserted(void)
{
	return fake_write_protect;
}

static int command_set_fake_wp(int argc, char **argv)
{
	int val;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	val = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	fake_write_protect = val;
	ccprintf("Fake write protect = %d\n", val);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fakewp, command_set_fake_wp,
			"<0 | 1>",
			"Set fake write protect pin",
			NULL);
