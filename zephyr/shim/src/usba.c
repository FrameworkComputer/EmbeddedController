/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_usba_port_enable_pins

#include <devicetree.h>
#include "hooks.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define PIN(node_id, prop, idx) GPIO_SIGNAL(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0,
		"No compatible USBA Port Enable instance found");

#define USBA_ENABLE_PINS(inst) DT_INST_FOREACH_PROP_ELEM(inst, enable_pins, PIN)

const int usb_port_enable[] = {
	DT_INST_FOREACH_STATUS_OKAY(USBA_ENABLE_PINS)
};

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
