/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "config.h"
#include "hooks.h"
#include "system.h"
#include "ish_persistent_data.h"

#define PERSISTENT_DATA_MAGIC 0x49534864 /* "ISHd" */

struct ish_persistent_data ish_persistent_data = {
	.magic = PERSISTENT_DATA_MAGIC,
	.reset_flags = EC_RESET_FLAG_POWER_ON,
	.watchdog_counter = 0,
	.panic_data = {0},
};

/*
 * When AON task firmware is not available (perhaps in the early
 * stages of bringing up a new board), we have no way to persist data
 * across reset. Allocate a memory region for "persistent data" which
 * will never persist, this way we can use ish_persistent_data in a
 * consistent manner without having to worry if the AON task firmware
 * is available.
 *
 * Otherwise (AON task firmware is available), the
 * ish_persistent_data_aon symbol is exported by the linker script.
 */
#ifdef CONFIG_ISH_PM_AONTASK
extern struct ish_persistent_data ish_persistent_data_aon;
#else
static struct ish_persistent_data ish_persistent_data_aon;
#endif

void ish_persistent_data_init(void)
{
	if (ish_persistent_data_aon.magic == PERSISTENT_DATA_MAGIC) {
		/* Stored data is valid, load a copy */
		memcpy(&ish_persistent_data,
		       &ish_persistent_data_aon,
		       sizeof(struct ish_persistent_data));

		/* Invalidate stored data, in case commit fails to happen */
		ish_persistent_data_aon.magic = 0;
	}

	/* Update the system module's copy of the reset flags */
	system_set_reset_flags(chip_read_reset_flags());
}

void ish_persistent_data_commit(void)
{
	memcpy(&ish_persistent_data_aon,
	       &ish_persistent_data,
	       sizeof(struct ish_persistent_data));
}
