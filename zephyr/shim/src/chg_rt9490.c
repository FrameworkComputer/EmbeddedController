/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT richtek_rt9490

#include "driver/charger/rt9490.h"

#include <zephyr/devicetree.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "This driver doesn't support multiple rt9490 instance");

#define RT9490_NODE DT_INST(0, DT_DRV_COMPAT)

const struct rt9490_init_setting rt9490_setting = {
	.eoc_current = DT_PROP(RT9490_NODE, eoc_current),
	.mivr = DT_PROP(RT9490_NODE, mivr),
	.boost_voltage = DT_PROP(RT9490_NODE, boost_voltage),
	.boost_current = DT_PROP(RT9490_NODE, boost_current),
};

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
