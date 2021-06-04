/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OTP implementation for STM32F411 */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "otp.h"
#include "registers.h"
#include "util.h"

/*
 * OTP is only used for saving the USB serial number.
 */
#ifdef CONFIG_SERIALNO_LEN
/* Which block to use */
#define OTP_SERIAL_BLOCK 0
#define OTP_SERIAL_ADDR \
	REG32_ADDR(STM32_OTP_BLOCK_DATA(OTP_SERIAL_BLOCK, 0))

/* Number of word used in the block */
#define OTP_SERIAL_BLOCK_SIZE (CONFIG_SERIALNO_LEN / sizeof(uint32_t))
BUILD_ASSERT(CONFIG_SERIALNO_LEN % sizeof(uint32_t) == 0);
BUILD_ASSERT(OTP_SERIAL_BLOCK_SIZE < STM32_OTP_BLOCK_SIZE);

/*
 * Write an OTP block
 *
 * @param block         block to write.
 * @param size          Number of words to write.
 * @param data          Destination buffer for data.
 */
static int otp_write(uint8_t block, int size, const char *data)
{
	if (block >= STM32_OTP_BLOCK_NB)
		return EC_ERROR_PARAM1;
	if (size >= STM32_OTP_BLOCK_SIZE)
		return EC_ERROR_PARAM2;
	return crec_flash_physical_write(STM32_OTP_BLOCK_DATA(block, 0) -
					 CONFIG_PROGRAM_MEMORY_BASE,
					 size * sizeof(uint32_t), data);
}

/*
 * Check if an OTP block is protected.
 *
 * @param block        protected block.
 * @return non-zero if that block is read only.
 */
static int otp_get_protect(uint8_t block)
{
	uint32_t lock;

	lock = REG32(STM32_OTP_LOCK(block));
	return ((lock & STM32_OPT_LOCK_MASK(block)) == 0);
}

/*
 * Set a particular OTP block as read only.
 *
 * @param block        block to protect.
 */
static int otp_set_protect(uint8_t block)
{
	int rv;
	uint32_t lock;

	if (otp_get_protect(block))
		return EC_SUCCESS;

	lock = REG32(STM32_OTP_LOCK(block));
	lock &= ~STM32_OPT_LOCK_MASK(block);
	rv = crec_flash_physical_write(STM32_OTP_LOCK(block) -
				       CONFIG_PROGRAM_MEMORY_BASE,
				       sizeof(uint32_t), (char *)&lock);
	if (rv)
		return rv;
	else
		return EC_SUCCESS;
}

const char *otp_read_serial(void)
{
	int i;

	for (i = 0; i < OTP_SERIAL_BLOCK_SIZE; i++) {
		if (OTP_SERIAL_ADDR[i] != -1)
			return (char *)OTP_SERIAL_ADDR;
	}
	return NULL;
}

int otp_write_serial(const char *serialno)
{
	int i, ret;
	char otp_serial[CONFIG_SERIALNO_LEN];

	if (otp_get_protect(OTP_SERIAL_BLOCK))
		return EC_ERROR_ACCESS_DENIED;

	/* Copy in serialno. */
	for (i = 0; i < CONFIG_SERIALNO_LEN - 1; i++) {
		otp_serial[i] = serialno[i];
		if (serialno[i] == 0)
			break;
	}
	for (; i < CONFIG_SERIALNO_LEN; i++)
		otp_serial[i] = 0;

	ret = otp_write(OTP_SERIAL_BLOCK, OTP_SERIAL_BLOCK_SIZE, otp_serial);
	if (ret == EC_SUCCESS)
		return otp_set_protect(OTP_SERIAL_BLOCK);
	else
		return ret;
}
#endif
