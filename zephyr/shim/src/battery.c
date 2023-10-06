/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_fuel_gauge.h"

#include <zephyr/devicetree.h>

#define NODE_FUEL_GAUGE(node) \
	{ \
	.manuf_name = DT_PROP(node, manuf_name), \
	.device_name = DT_PROP(node, device_name), \
	.flags = DT_PROP_OR(node, flags, 0), \
	.ship_mode = { \
		.reg_addr = DT_PROP(node, ship_mode_reg_addr), \
		.reg_data = DT_PROP(node, ship_mode_reg_data), \
	}, \
	.sleep_mode = { \
		.reg_addr = DT_PROP_OR(node, sleep_mode_reg_addr, 0), \
		.reg_data = DT_PROP_OR(node, sleep_mode_reg_data, 0), \
	}, \
	.fet = { \
		.reg_addr = DT_PROP_OR(node, fet_reg_addr, 0), \
		.reg_mask = DT_PROP(node, fet_reg_mask), \
		.disconnect_val = DT_PROP(node, fet_disconnect_val), \
		.cfet_mask = DT_PROP_OR(node, fet_cfet_mask, 0), \
		.cfet_off_val = DT_PROP_OR(node, fet_cfet_off_val, 0), \
	}, \
	COND_CODE_1(UTIL_AND(IS_ENABLED(CONFIG_BATTERY_MEASURE_IMBALANCE), \
			     DT_NODE_HAS_PROP(node, imbalance_mv)), \
		(.imbalance_mv = DT_STRING_TOKEN(node, imbalance_mv),), ()) \
},

#define NODE_BATT_INFO(node)                                                 \
	{                                                                    \
		.voltage_max = DT_PROP(node, voltage_max),                   \
		.voltage_normal = DT_PROP(node, voltage_normal),             \
		.voltage_min = DT_PROP(node, voltage_min),                   \
		.precharge_voltage = DT_PROP_OR(node, precharge_voltage, 0), \
		.precharge_current = DT_PROP_OR(node, precharge_current, 0), \
		.start_charging_min_c = DT_PROP(node, start_charging_min_c), \
		.start_charging_max_c = DT_PROP(node, start_charging_max_c), \
		.charging_min_c = DT_PROP(node, charging_min_c),             \
		.charging_max_c = DT_PROP(node, charging_max_c),             \
		.discharging_min_c = DT_PROP(node, discharging_min_c),       \
		.discharging_max_c = DT_PROP(node, discharging_max_c),       \
	},

#define NODE_BATT_PARAMS(node)                            \
	{ .fuel_gauge = NODE_FUEL_GAUGE(node).batt_info = \
		  NODE_BATT_INFO(node) },

#if DT_HAS_COMPAT_STATUS_OKAY(battery_smart)

const struct board_batt_params board_battery_info[] = { DT_FOREACH_STATUS_OKAY(
	battery_smart, NODE_BATT_PARAMS) };

#if DT_NODE_EXISTS(DT_NODELABEL(default_battery))
#define BAT_ENUM(node) DT_CAT(BATTERY_, node)
const enum battery_type DEFAULT_BATTERY_TYPE =
	BATTERY_TYPE(DT_NODELABEL(default_battery));
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(default_battery_3s))
#define BAT_ENUM(node) DT_CAT(BATTERY_, node)
const enum battery_type DEFAULT_BATTERY_TYPE_3S =
	BATTERY_TYPE(DT_NODELABEL(default_battery_3s));
#endif
#endif /* DT_HAS_COMPAT_STATUS_OKAY(battery_smart) */
