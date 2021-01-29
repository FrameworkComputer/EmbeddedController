/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_BBRAM_H_
#define ZEPHYR_SHIM_INCLUDE_BBRAM_H_

/**
 * Layout of the battery-backed RAM region.
 * TODO (b:178807203) Migrate these values to devicetree registers.
 */
enum bbram_data_index {
	/** General-purpose scratchpad */
	BBRM_DATA_INDEX_SCRATCHPAD = 0,
	/** Saved reset flags */
	BBRM_DATA_INDEX_SAVED_RESET_FLAGS = 4,
	/** Wake reasons for hibernate */
	BBRM_DATA_INDEX_WAKE = 8,
	/** USB-PD saved port0 state */
	BBRM_DATA_INDEX_PD0 = 12,
	/** USB-PD saved port1 state */
	BBRM_DATA_INDEX_PD1 = 13,
	/** Vboot EC try slot */
	BBRM_DATA_INDEX_TRY_SLOT = 14,
	/** USB-PD saved port2 state */
	BBRM_DATA_INDEX_PD2 = 15,
	/** VbNvContext for ARM arch */
	BBRM_DATA_INDEX_VBNVCNTXT = 16,
	/** RAM log for Booter */
	BBRM_DATA_INDEX_RAMLOG = 32,
	/** Flag to indicate validity of panic data starting at index 36. */
	BBRM_DATA_INDEX_PANIC_FLAGS = 35,
	/** Panic data (index 35-63)*/
	BBRM_DATA_INDEX_PANIC_BKUP = 36,
	/** The start time of LCT(4 bytes) */
	BBRM_DATA_INDEX_LCT_TIME = 64,
};

#endif /* ZEPHYR_SHIM_INCLUDE_BBRAM_H_ */
