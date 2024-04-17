/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charger.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

__override void board_power_change(struct ap_power_ev_callback *cb,
				   struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		break;
	}
}
