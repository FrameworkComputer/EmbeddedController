/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_bbram.h>
#include <logging/log.h>

#include "system.h"

#define GET_BBRAM_OFFSET(node) \
	DT_PROP(DT_PATH(named_bbram_regions, node), offset)
#define GET_BBRAM_SIZE(node) DT_PROP(DT_PATH(named_bbram_regions, node), size)

LOG_MODULE_REGISTER(shim_ite_system, LOG_LEVEL_ERR);

const struct device *bbram_dev;

void chip_save_reset_flags(uint32_t flags)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't have a binding");
		return;
	}

	cros_bbram_write(bbram_dev, GET_BBRAM_OFFSET(saved_reset_flags),
			 GET_BBRAM_SIZE(saved_reset_flags), (uint8_t *)&flags);
}

uint32_t chip_read_reset_flags(void)
{
	uint32_t flags = 0;

	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't have a binding");
		return 0;
	}

	cros_bbram_read(bbram_dev, GET_BBRAM_OFFSET(saved_reset_flags),
			GET_BBRAM_SIZE(saved_reset_flags), (uint8_t *)&flags);

	return flags;
}

int system_set_scratchpad(uint32_t value)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't have a binding");
		return 0;
	}

	cros_bbram_write(bbram_dev, GET_BBRAM_OFFSET(scratchpad),
			 GET_BBRAM_SIZE(scratchpad), (uint8_t *)&value);

	return EC_SUCCESS;
}

uint32_t system_get_scratchpad(void)
{
	uint32_t value = 0;

	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't have a binding");
		return 0;
	}

	cros_bbram_read(bbram_dev, GET_BBRAM_OFFSET(scratchpad),
			GET_BBRAM_SIZE(scratchpad), (uint8_t *)&value);

	return value;
}

static int chip_system_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));
	if (!device_is_ready(bbram_dev)) {
		LOG_ERR("Error: device %s is not ready", bbram_dev->name);
		return -1;
	}

	return 0;
}
SYS_INIT(chip_system_init, PRE_KERNEL_1, 15);

uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	/*
	 * Because our reset vector is at the beginning of image copy
	 * (see init.S). So I just need to return 'base' here and EC will jump
	 * to the reset vector.
	 */
	return base;
}
