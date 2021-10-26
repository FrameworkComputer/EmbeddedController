/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_POWER_POWER_H
#define ZEPHYR_CHROME_POWER_POWER_H

#include <devicetree.h>

#define POWER_SIGNAL_LIST_NODE                                                \
	DT_NODELABEL(power_signal_list)

#define SYSTEM_DT_POWER_SIGNAL_CONFIG                                         \
	DT_NODE_EXISTS(POWER_SIGNAL_LIST_NODE)

#if (SYSTEM_DT_POWER_SIGNAL_CONFIG)

#define GEN_POWER_SIGNAL_STRUCT_ENTRY_GPIO(cid)                               \
	DT_STRING_UPPER_TOKEN(                                                \
		DT_PROP(                                                      \
			cid,                                                  \
			gpio                                                  \
		),                                                            \
		enum_name                                                     \
	)
#define GEN_POWER_SIGNAL_STRUCT_ENTRY_FLAGS(cid)                              \
(                                                                             \
	DT_GPIO_FLAGS(                                                        \
		DT_PROP(                                                      \
			cid,                                                  \
			gpio                                                  \
		),                                                            \
		gpios                                                         \
	) & GPIO_ACTIVE_LOW                                                   \
		? POWER_SIGNAL_ACTIVE_LOW                                     \
		: POWER_SIGNAL_ACTIVE_HIGH                                    \
)
#define GEN_POWER_SIGNAL_STRUCT_ENTRY_NAME(cid)                               \
	DT_PROP(                                                              \
		cid,                                                          \
		power_enum_name                                               \
	)

#define GEN_POWER_SIGNAL_STRUCT_ENTRY(cid)                                    \
{                                                                             \
	.gpio = GEN_POWER_SIGNAL_STRUCT_ENTRY_GPIO(cid),                      \
	.flags = GEN_POWER_SIGNAL_STRUCT_ENTRY_FLAGS(cid),                    \
	.name = GEN_POWER_SIGNAL_STRUCT_ENTRY_NAME(cid)                       \
}
#define GEN_POWER_SIGNAL_STRUCT(cid)                                          \
	[GEN_POWER_SIGNAL_ENUM_ENTRY(cid)] =                                  \
		GEN_POWER_SIGNAL_STRUCT_ENTRY(cid),


#define GEN_POWER_SIGNAL_ENUM_ENTRY(cid)                                      \
	DT_STRING_UPPER_TOKEN(                                                \
		cid,                                                          \
		power_enum_name                                               \
	)
#define GEN_POWER_SIGNAL_ENUM_ENTRY_COMMA(cid)                                \
	GEN_POWER_SIGNAL_ENUM_ENTRY(cid),

enum power_signal {
	DT_FOREACH_CHILD(
		POWER_SIGNAL_LIST_NODE,
		GEN_POWER_SIGNAL_ENUM_ENTRY_COMMA)
	POWER_SIGNAL_COUNT
};

/*
 * Verify the number of required power-signals are specified in
 * the DeviceTree
 */
#define POWER_SIGNALS_REQUIRED                                                \
	DT_PROP(                                                              \
		POWER_SIGNAL_LIST_NODE,                                       \
		power_signals_required                                        \
	)
BUILD_ASSERT(POWER_SIGNALS_REQUIRED == POWER_SIGNAL_COUNT);

#endif /* SYSTEM_DT_POWER_SIGNAL_CONFIG */
#endif /* ZEPHYR_CHROME_POWER_POWER_H */
