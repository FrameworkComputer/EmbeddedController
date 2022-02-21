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
 * Add new entries at the end of the enum. Otherwise you will break RO/RW
 * compatibility.
 */
enum bkpdata_index {
	BKPDATA_INDEX_SCRATCHPAD,	     /* General-purpose scratchpad */
	BKPDATA_INDEX_SAVED_RESET_FLAGS,     /* Saved reset flags */
#ifdef CONFIG_STM32_EXTENDED_RESET_FLAGS
	BKPDATA_INDEX_SAVED_RESET_FLAGS_2,   /* Saved reset flags (cont) */
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
#ifdef CONFIG_SOFTWARE_PANIC
	/**
	 * Saving the panic flags in case that AP thinks the panic is new
	 * after a hard reset.
	 */
	BKPDATA_INDEX_SAVED_PANIC_FLAGS,     /* Saved panic flags */
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
