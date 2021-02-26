/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_bbram.h>
#include <logging/log.h>

#include "system.h"

#define GET_BBRAM_OFFSET(node) \
	DT_PROP(DT_PATH(named_bbram_regions, node), offset)
#define GET_BBRAM_SIZE(node) DT_PROP(DT_PATH(named_bbram_regions, node), size)

LOG_MODULE_REGISTER(shim_npcx_system, LOG_LEVEL_ERR);

const static struct device *bbram_dev;

void chip_save_reset_flags(uint32_t flags)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return;
	}

	cros_bbram_write(bbram_dev, GET_BBRAM_OFFSET(saved_reset_flags),
			 GET_BBRAM_SIZE(saved_reset_flags), (uint8_t *)&flags);
}

uint32_t chip_read_reset_flags(void)
{
	uint32_t flags;

	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return 0;
	}

	cros_bbram_read(bbram_dev, GET_BBRAM_OFFSET(saved_reset_flags),
			GET_BBRAM_SIZE(saved_reset_flags), (uint8_t *)&flags);

	return flags;
}

void chip_bbram_status_check(void)
{
	if (!bbram_dev) {
		LOG_DBG("bbram_dev doesn't binding");
		return;
	}

	if (cros_bbram_get_ibbr(bbram_dev)) {
		LOG_ERR("VBAT power drop!");
		cros_bbram_reset_ibbr(bbram_dev);
	}
	if (cros_bbram_get_vsby(bbram_dev)) {
		LOG_ERR("VSBY power drop!");
		cros_bbram_reset_vsby(bbram_dev);
	}
	if (cros_bbram_get_vcc1(bbram_dev)) {
		LOG_ERR("VCC1 power drop!");
		cros_bbram_reset_vcc1(bbram_dev);
	}
}

static int chip_system_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	/*
	 * NPCX chip uses BBRAM to save the reset flag. Binding & check BBRAM
	 * here.
	 */
	bbram_dev = device_get_binding(DT_LABEL(DT_NODELABEL(bbram)));
	if (!bbram_dev) {
		LOG_ERR("bbram_dev gets binding failed");
		return -1;
	}

	/* check the BBRAM status */
	chip_bbram_status_check();

	return 0;
}
/*
 * The priority should be lower than CROS_BBRAM_NPCX_INIT_PRIORITY.
 */
#if (CONFIG_CROS_SYSTEM_NPCX_PRE_INIT_PRIORITY <= \
     CONFIG_CROS_BBRAM_NPCX_INIT_PRIORITY)
#error CONFIG_CROS_SYSTEM_NPCX_PRE_INIT_PRIORITY must greater than \
	CONFIG_CROS_BBRAM_NPCX_INIT_PRIORITY
#endif
SYS_INIT(chip_system_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_NPCX_PRE_INIT_PRIORITY);
