/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/util.h>

#include "console.h"
#include "power.h"
#include "power/power.h"

#if (SYSTEM_DT_POWER_SIGNAL_CONFIG)

const struct power_signal_info power_signal_list[] = {
	DT_FOREACH_CHILD(
		POWER_SIGNAL_LIST_NODE,
		GEN_POWER_SIGNAL_STRUCT)
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

#endif /* SYSTEM_DT_POWER_SIGNAL_CONFIG */
