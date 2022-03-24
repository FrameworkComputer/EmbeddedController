/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#error "This file must only be included from config_chip.h and it should be" \
	"included in all zephyr builds automatically"
#endif

#define BATTERY_ENUM(val)	DT_CAT(BATTERY_, val)
#define BATTERY_TYPE(id) BATTERY_ENUM(DT_STRING_UPPER_TOKEN(id, enum_name))
#define BATTERY_TYPE_WITH_COMMA(id)	BATTERY_TYPE(id),

/* This produces a list of BATTERY_<ENUM_NAME> identifiers */
enum battery_type {
#if DT_HAS_COMPAT_STATUS_OKAY(battery_smart)
	DT_FOREACH_STATUS_OKAY(battery_smart, BATTERY_TYPE_WITH_COMMA)
#endif
	BATTERY_TYPE_COUNT,
};

#undef BATTERY_TYPE_WITH_COMMA
