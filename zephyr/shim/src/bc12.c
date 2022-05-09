/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "usbc/bc12_pi3usb9201.h"
#include "usbc/bc12_rt1739.h"
#include "usbc/bc12_rt9490.h"
#include "usbc/utils.h"
#include "usb_charge.h"

#if DT_HAS_COMPAT_STATUS_OKAY(RT1739_BC12_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(RT9490_BC12_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(PI3USB9201_COMPAT)

#define BC12_CHIP(id, fn) [USBC_PORT(id)] = fn(id)

/* Power Path Controller */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	DT_FOREACH_STATUS_OKAY_VARGS(RT1739_BC12_COMPAT, BC12_CHIP,
				     BC12_CHIP_RT1739)
	DT_FOREACH_STATUS_OKAY_VARGS(RT9490_BC12_COMPAT, BC12_CHIP,
				     BC12_CHIP_RT9490)
	DT_FOREACH_STATUS_OKAY_VARGS(PI3USB9201_COMPAT, BC12_CHIP,
				     BC12_CHIP_PI3USB9201)
};

#endif /* #if DT_HAS_COMPAT_STATUS_OKAY */
