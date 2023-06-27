/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "power.h"
#include "power/power.h"

#include <zephyr/sys/util.h>

#define GEN_POWER_SIGNAL_STRUCT_ENTRY_GPIO(cid) \
	DT_STRING_UPPER_TOKEN(DT_PROP(cid, power_gpio_pin), enum_name)
#define GEN_POWER_SIGNAL_STRUCT_ENTRY_FLAGS(cid)              \
	(DT_GPIO_FLAGS(DT_PROP(cid, power_gpio_pin), gpios) & \
			 GPIO_ACTIVE_LOW ?                    \
		 POWER_SIGNAL_ACTIVE_LOW :                    \
		 POWER_SIGNAL_ACTIVE_HIGH)
#define GEN_POWER_SIGNAL_STRUCT_ENTRY_NAME(cid) DT_PROP(cid, power_enum_name)

#define GEN_POWER_SIGNAL_STRUCT_ENTRY(cid)                         \
	{                                                          \
		.gpio = GEN_POWER_SIGNAL_STRUCT_ENTRY_GPIO(cid),   \
		.flags = GEN_POWER_SIGNAL_STRUCT_ENTRY_FLAGS(cid), \
		.name = GEN_POWER_SIGNAL_STRUCT_ENTRY_NAME(cid)    \
	}
#define GEN_POWER_SIGNAL_STRUCT(cid) \
	[GEN_POWER_SIGNAL_ENUM_ENTRY(cid)] = GEN_POWER_SIGNAL_STRUCT_ENTRY(cid),

const struct power_signal_info power_signal_list[] = { DT_FOREACH_CHILD(
	POWER_SIGNAL_LIST_NODE, GEN_POWER_SIGNAL_STRUCT) };
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/*
 * Verify the number of required power-signals are specified in
 * the DeviceTree
 */
#define POWER_SIGNALS_REQUIRED \
	DT_PROP(POWER_SIGNAL_LIST_NODE, power_signals_required)
BUILD_ASSERT(POWER_SIGNALS_REQUIRED == POWER_SIGNAL_COUNT);
