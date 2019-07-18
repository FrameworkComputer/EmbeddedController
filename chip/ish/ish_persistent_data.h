/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ISH_PERSISTENT_DATA_H
#define __CROS_EC_ISH_PERSISTENT_DATA_H

#include "panic.h"

/*
 * If you make backwards-incompatible changes to this struct, (that
 * is, reading a previous version of the data would be incorrect),
 * simply change the magic number in ish_persistent_data.c. This will
 * cause the struct to be re-initialized when the firmware loads.
 */
struct ish_persistent_data {
	uint32_t magic;
	uint32_t reset_flags;
	uint32_t watchdog_counter;
	struct panic_data panic_data;
};

/*
 * Local copy of persistent data, which is copied from AON memory only
 * if the data in AON memory is valid.
 */
extern struct ish_persistent_data ish_persistent_data;

/*
 * Copy the AON persistent data into the local copy and initialize
 * system reset flags, only if magic number is correct.
 */
void ish_persistent_data_init(void);

/*
 * Commit the local copy to the AON memory (to be called at reset).
 */
void ish_persistent_data_commit(void);

/**
 * SNOWBALL - registers about UMA/IMR DDR information and FW location
 * in it.  ISH Bringup will set these register values at boot
 */
struct snowball_struct {
	uint32_t reserved[28];
	uint32_t volatile uma_base_hi;
	uint32_t volatile uma_base_lo;
	uint32_t volatile uma_limit;
	uint32_t volatile fw_offset;
};

#endif /* __CROS_EC_ISH_PERSISTENT_DATA_H */
