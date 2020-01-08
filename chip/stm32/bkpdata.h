/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Backup data functionality */

#ifndef __CROS_EC_BKPDATA_H
#define __CROS_EC_BKPDATA_H

#include "common.h"
#include "registers.h"
#include "system.h" /* enum system_bbram_idx */

/* We use 16-bit BKP / BBRAM entries. */
#define STM32_BKP_ENTRIES (STM32_BKP_BYTES / 2)

/*
 * Use 32-bit for reset flags, if we have space for it:
 *  - 2 indexes are used unconditionally (SCRATCHPAD and SAVED_RESET_FLAGS)
 *  - VBNV_CONTEXT requires 8 indexes, so a total of 10 (which is the total
 *    number of entries on some STM32 variants).
 *  - Other config options are not a problem (they only take a few entries)
 *
 * Given this, we can only add an extra entry for the top 16-bit of reset flags
 * if VBNV_CONTEXT is not enabled, or if we have more than 10 entries.
 */
#if !defined(CONFIG_HOSTCMD_VBNV_CONTEXT) || STM32_BKP_ENTRIES > 10
#define CONFIG_STM32_RESET_FLAGS_EXTENDED
#endif

enum bkpdata_index {
	BKPDATA_INDEX_SCRATCHPAD,	     /* General-purpose scratchpad */
	BKPDATA_INDEX_SAVED_RESET_FLAGS,     /* Saved reset flags */
#ifdef CONFIG_STM32_RESET_FLAGS_EXTENDED
	BKPDATA_INDEX_SAVED_RESET_FLAGS_2,   /* Saved reset flags (cont) */
#endif
#ifdef CONFIG_HOSTCMD_VBNV_CONTEXT
	BKPDATA_INDEX_VBNV_CONTEXT0,
	BKPDATA_INDEX_VBNV_CONTEXT1,
	BKPDATA_INDEX_VBNV_CONTEXT2,
	BKPDATA_INDEX_VBNV_CONTEXT3,
	BKPDATA_INDEX_VBNV_CONTEXT4,
	BKPDATA_INDEX_VBNV_CONTEXT5,
	BKPDATA_INDEX_VBNV_CONTEXT6,
	BKPDATA_INDEX_VBNV_CONTEXT7,
#endif
#ifdef CONFIG_SOFTWARE_PANIC
	BKPDATA_INDEX_SAVED_PANIC_REASON,    /* Saved panic reason */
	BKPDATA_INDEX_SAVED_PANIC_INFO,      /* Saved panic data */
	BKPDATA_INDEX_SAVED_PANIC_EXCEPTION, /* Saved panic exception code */
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE
	BKPDATA_INDEX_PD0,		     /* USB-PD saved port0 state */
	BKPDATA_INDEX_PD1,		     /* USB-PD saved port1 state */
	BKPDATA_INDEX_PD2,		     /* USB-PD saved port2 state */
#endif
	BKPDATA_COUNT
};
BUILD_ASSERT(STM32_BKP_ENTRIES >= BKPDATA_COUNT);

/**
 * Read backup register at specified index.
 *
 * @return The value of the register or 0 if invalid index.
 */
uint16_t bkpdata_read(enum bkpdata_index index);

/**
 * Write hibernate register at specified index.
 *
 * @return nonzero if error.
 */
int bkpdata_write(enum bkpdata_index index, uint16_t value);

int bkpdata_index_lookup(enum system_bbram_idx idx, int *msb);
uint32_t bkpdata_read_reset_flags(void);
void bkpdata_write_reset_flags(uint32_t save_flags);

#endif /* __CROS_EC_BKPDATA_H */
