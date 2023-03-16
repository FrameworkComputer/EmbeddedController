/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "usbc/usb_muxes.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(skyrim, CONFIG_SKYRIM_LOG_LEVEL);

/*
 * Provide weak definitons for interrupt handlers. This minimizes the number
 * of test-specific device tree overrides needed.
 */

test_mockable int board_anx7483_c0_mux_set(const struct usb_mux *me,
					   mux_state_t mux_state)
{
	return -EINVAL;
}

test_mockable int board_anx7483_c1_mux_set(const struct usb_mux *me,
					   mux_state_t mux_state)
{
	return -EINVAL;
}
