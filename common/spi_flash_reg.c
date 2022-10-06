/*
 * Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SPI flash protection register translation functions for Chrome OS EC.
 */

#include "common.h"
#include "spi_flash_reg.h"
#include "util.h"

/* Bit state for protect range table */
enum bit_state {
	OFF = 0,
	ON = 1,
	IGN = -1, /* Don't care / Ignore */
};

struct protect_range {
	enum bit_state cmp;
	enum bit_state sec;
	enum bit_state tb;
	enum bit_state bp[3]; /* Ordered {BP2, BP1, BP0} */
	uint32_t protect_start;
	uint32_t protect_len;
};

/* Compare macro for (x =? b) for 'IGN' comparison */
#define COMPARE_BIT(a, b) ((a) != IGN && (a) != !!(b))
/* Assignment macro where 'IGN' = 0 */
#define GET_BIT(a) ((a) == IGN ? 0 : (a))

/*
 * Define flags and protect table for each SPI ROM part. It's not necessary
 * to define all ranges in the datasheet since we'll usually protect only
 * none or half of the ROM. The table is searched sequentially, so ordering
 * according to likely configurations improves performance slightly.
 */
#if defined(CONFIG_SPI_FLASH_W25X40) || defined(CONFIG_SPI_FLASH_GD25Q41B)
static const struct protect_range spi_flash_protect_ranges[] = {
	{ IGN, IGN, IGN, { 0, 0, 0 }, 0, 0 }, /* No protection */
	{ IGN, IGN, 1, { 0, 1, 1 }, 0, 0x40000 }, /* Lower 1/2 */
	{ IGN, IGN, 1, { 0, 1, 0 }, 0, 0x20000 }, /* Lower 1/4 */
};

#elif defined(CONFIG_SPI_FLASH_W25Q40) || defined(CONFIG_SPI_FLASH_GD25LQ40)
/* Verified for W25Q40BV and W25Q40EW */
/* For GD25LQ40, BP3 and BP4 have same meaning as TB and SEC */
static const struct protect_range spi_flash_protect_ranges[] = {
	/* CMP = 0 */
	{ 0, IGN, IGN, { 0, 0, 0 }, 0, 0 }, /* No protection */
	{ 0, 0, 1, { 0, 1, 0 }, 0, 0x20000 }, /* Lower 1/4 */
	{ 0, 0, 1, { 0, 1, 1 }, 0, 0x40000 }, /* Lower 1/2 */
	/* CMP = 1 */
	{ 1, 0, 0, { 0, 1, 1 }, 0, 0x40000 }, /* Lower 1/2 */
	{ 1, 0, IGN, { 1, IGN, IGN }, 0, 0 }, /* None (W25Q40EW only) */
};

#elif defined(CONFIG_SPI_FLASH_W25Q64)
static const struct protect_range spi_flash_protect_ranges[] = {
	{ 0, IGN, IGN, { 0, 0, 0 }, 0, 0 }, /* No protection */
	{ 0, 0, 1, { 1, 1, 0 }, 0, 0x400000 }, /* Lower 1/2 */
	{ 0, 0, 1, { 1, 0, 1 }, 0, 0x200000 }, /* Lower 1/4 */
};

#elif defined(CONFIG_SPI_FLASH_W25Q80)
static const struct protect_range spi_flash_protect_ranges[] = {
	/* CMP = 0 */
	{ 0, IGN, IGN, { 0, 0, 0 }, 0, 0 }, /* No protection */
	{ 0, 0, 1, { 0, 1, 0 }, 0, 0x20000 }, /* Lower 1/8 */
	{ 0, 0, 1, { 0, 1, 1 }, 0, 0x40000 }, /* Lower 1/4 */
	{ 0, 0, 1, { 1, 0, 0 }, 0, 0x80000 }, /* Lower 1/2 */
};
#elif defined(CONFIG_SPI_FLASH_W25Q128)
static const struct protect_range spi_flash_protect_ranges[] = {
	/* CMP = 0 */
	{ 0, IGN, IGN, { 0, 0, 0 }, 0, 0 }, /* No protection */
	{ 0, 0, 1, { 1, 0, 0 }, 0, 0x20000 }, /* Lower 1/8 */
	{ 0, 0, 1, { 1, 0, 1 }, 0, 0x40000 }, /* Lower 1/4 */
	{ 0, 0, 1, { 1, 1, 0 }, 0, 0x80000 }, /* Lower 1/2 */
};
#endif

/**
 * Computes block write protection range from registers
 * Returns start == len == 0 for no protection
 *
 * @param sr1 Status register 1
 * @param sr2 Status register 2
 * @param start Output pointer for protection start offset
 * @param len Output pointer for protection length
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_reg_to_protect(uint8_t sr1, uint8_t sr2, unsigned int *start,
			     unsigned int *len)
{
	const struct protect_range *range;
	int i;
	uint8_t cmp;
	uint8_t sec;
	uint8_t tb;
	uint8_t bp;

	/* Determine flags */
	cmp = (sr2 & SPI_FLASH_SR2_CMP) ? 1 : 0;
	sec = (sr1 & SPI_FLASH_SR1_SEC) ? 1 : 0;
	tb = (sr1 & SPI_FLASH_SR1_TB) ? 1 : 0;
	bp = (sr1 &
	      (SPI_FLASH_SR1_BP2 | SPI_FLASH_SR1_BP1 | SPI_FLASH_SR1_BP0)) >>
	     2;

	/* Bad pointers or invalid data */
	if (!start || !len || sr1 == 0xff || sr2 == 0xff)
		return EC_ERROR_INVAL;

	for (i = 0; i < ARRAY_SIZE(spi_flash_protect_ranges); ++i) {
		range = &spi_flash_protect_ranges[i];
		if (COMPARE_BIT(range->cmp, cmp))
			continue;
		if (COMPARE_BIT(range->sec, sec))
			continue;
		if (COMPARE_BIT(range->tb, tb))
			continue;
		if (COMPARE_BIT(range->bp[0], bp & 0x4))
			continue;
		if (COMPARE_BIT(range->bp[1], bp & 0x2))
			continue;
		if (COMPARE_BIT(range->bp[2], bp & 0x1))
			continue;

		*start = range->protect_start;
		*len = range->protect_len;
		return EC_SUCCESS;
	}

	/* Invalid range, or valid range missing from our table */
	return EC_ERROR_INVAL;
}

/**
 * Computes block write protection registers from range
 *
 * @param start Desired protection start offset
 * @param len Desired protection length
 * @param sr1 Output pointer for status register 1
 * @param sr2 Output pointer for status register 2
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_flash_protect_to_reg(unsigned int start, unsigned int len, uint8_t *sr1,
			     uint8_t *sr2)
{
	const struct protect_range *range;
	int i;
	char cmp = 0;
	char sec = 0;
	char tb = 0;
	char bp = 0;

	/* Bad pointers */
	if (!sr1 || !sr2)
		return EC_ERROR_INVAL;

	/* Invalid data */
	if ((start && !len) || start + len > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	for (i = 0; i < ARRAY_SIZE(spi_flash_protect_ranges); ++i) {
		range = &spi_flash_protect_ranges[i];
		if (range->protect_start == start &&
		    range->protect_len == len) {
			cmp = GET_BIT(range->cmp);
			sec = GET_BIT(range->sec);
			tb = GET_BIT(range->tb);
			bp = GET_BIT(range->bp[0]) << 2 |
			     GET_BIT(range->bp[1]) << 1 | GET_BIT(range->bp[2]);

			*sr1 = (sec ? SPI_FLASH_SR1_SEC : 0) |
			       (tb ? SPI_FLASH_SR1_TB : 0) | (bp << 2);
			*sr2 = (cmp ? SPI_FLASH_SR2_CMP : 0);
			return EC_SUCCESS;
		}
	}

	/* Invalid range, or valid range missing from our table */
	return EC_ERROR_INVAL;
}
