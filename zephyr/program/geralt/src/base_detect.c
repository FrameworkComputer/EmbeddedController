/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_state.h"
#include "tablet_mode.h"

#include <zephyr/init.h>

static void base_update(bool attached)
{
	base_set_state(attached);
	tablet_set_mode(!attached, TABLET_TRIGGER_BASE);
}

static int base_init(const struct device *unused)
{
	/* TODO: this is a temporary fix to force tablet mode for developers. */
	base_update(false);

	return 0;
}

SYS_INIT(base_init, APPLICATION, 1);

void base_force_state(enum ec_set_base_state_cmd state)
{
	/* TODO: not implemented yet */
	base_update(state == EC_SET_BASE_STATE_ATTACH);
}
