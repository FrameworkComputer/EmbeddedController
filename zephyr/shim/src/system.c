/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <drivers/cros_bbram.h>

#include "bbram.h"
#include "common.h"
#include "cros_version.h"
#include "system.h"

STATIC_IF_NOT(CONFIG_ZTEST) const struct device *bbram_dev;

#if DT_NODE_EXISTS(DT_NODELABEL(bbram))
static int system_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	bbram_dev = device_get_binding(DT_LABEL(DT_NODELABEL(bbram)));
	return 0;
}

SYS_INIT(system_init, PRE_KERNEL_1, 50);
#endif

/* Return true if index is stored as a single byte in bbram */
static int bbram_is_byte_access(enum bbram_data_index index)
{
	return index == BBRM_DATA_INDEX_PD0 || index == BBRM_DATA_INDEX_PD1 ||
	       index == BBRM_DATA_INDEX_PD2 ||
	       index == BBRM_DATA_INDEX_PANIC_FLAGS;
}

/* Map idx to a returned BBRM_DATA_INDEX_*, or return -1 on invalid idx */
static int bbram_idx_lookup(enum system_bbram_idx idx)
{
	if (idx == SYSTEM_BBRAM_IDX_PD0)
		return BBRM_DATA_INDEX_PD0;
	if (idx == SYSTEM_BBRAM_IDX_PD1)
		return BBRM_DATA_INDEX_PD1;
	if (idx == SYSTEM_BBRAM_IDX_PD2)
		return BBRM_DATA_INDEX_PD2;
	if (idx == SYSTEM_BBRAM_IDX_TRY_SLOT)
		return BBRM_DATA_INDEX_TRY_SLOT;
	return -1;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int bbram_idx = bbram_idx_lookup(idx);
	int bytes, rc;

	if (bbram_idx < 0 || bbram_dev == NULL)
		return EC_ERROR_INVAL;

	bytes = bbram_is_byte_access(bbram_idx) ? 1 : 4;
	rc = ((struct cros_bbram_driver_api *)bbram_dev->api)
		     ->read(bbram_dev, bbram_idx, bytes, value);
	return rc ? EC_ERROR_INVAL : EC_SUCCESS;
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/*
	 * TODO(b:173787365): implement this.  For now, doing nothing
	 * won't break anything, just will eat power.
	 */
}

const char *system_get_chip_vendor(void)
{
	return "chromeos";
}

const char *system_get_chip_name(void)
{
	return "emu";
}

const char *system_get_chip_revision(void)
{
	return "";
}

const void *__image_size;
