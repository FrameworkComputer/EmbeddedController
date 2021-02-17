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

#define BBRAM_REGION_PD0	DT_PATH(named_bbram_regions, pd0)
#define BBRAM_REGION_PD1	DT_PATH(named_bbram_regions, pd1)
#define BBRAM_REGION_PD2	DT_PATH(named_bbram_regions, pd2)
#define BBRAM_REGION_TRY_SLOT	DT_PATH(named_bbram_regions, try_slot)

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

/* Map idx to a bbram offset/size, or return -1 on invalid idx */
static int bbram_lookup(enum system_bbram_idx idx, int *offset_out,
			int *size_out)
{
	switch (idx) {
	case SYSTEM_BBRAM_IDX_PD0:
		*offset_out = DT_PROP(BBRAM_REGION_PD0, offset);
		*size_out = DT_PROP(BBRAM_REGION_PD0, size);
		break;
	case SYSTEM_BBRAM_IDX_PD1:
		*offset_out = DT_PROP(BBRAM_REGION_PD1, offset);
		*size_out = DT_PROP(BBRAM_REGION_PD1, size);
		break;
	case SYSTEM_BBRAM_IDX_PD2:
		*offset_out = DT_PROP(BBRAM_REGION_PD2, offset);
		*size_out = DT_PROP(BBRAM_REGION_PD2, size);
		break;
	case SYSTEM_BBRAM_IDX_TRY_SLOT:
		*offset_out = DT_PROP(BBRAM_REGION_TRY_SLOT, offset);
		*size_out = DT_PROP(BBRAM_REGION_TRY_SLOT, size);
		break;
	default:
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int offset, size, rc;

	if (bbram_dev == NULL)
		return EC_ERROR_INVAL;

	rc = bbram_lookup(idx, &offset, &size);
	if (rc)
		return rc;

	rc = ((struct cros_bbram_driver_api *)bbram_dev->api)
		     ->read(bbram_dev, offset, size, value);
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
