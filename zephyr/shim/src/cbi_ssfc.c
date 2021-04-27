/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_ssfc.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "motionsense_sensors.h"
#include "motion_sense.h"
#include <logging/log.h>

#define DT_DRV_COMPAT named_cbi_ssfc_value

LOG_MODULE_REGISTER(cbi_ssfc_shim);

static union cbi_ssfc cached_ssfc;

#define REPLACE_ALT_MOTION_SENSOR(new_id, old_id) \
	motion_sensors[SENSOR_ID(old_id)] =       \
		motion_sensors_alt[SENSOR_ID(new_id)];

#define ALT_MOTION_SENSOR_INIT_ID(id)                            \
	COND_CODE_1(DT_NODE_HAS_PROP(id, alternate_for),         \
		    (REPLACE_ALT_MOTION_SENSOR(                  \
			    id, DT_PHANDLE(id, alternate_for))), \
		    ())

#define ALT_MOTION_SENSOR_INIT(i, id) \
	ALT_MOTION_SENSOR_INIT_ID(DT_PHANDLE_BY_IDX(id, devices, i))

#define SSFC_ALT_MOTION_SENSOR_INIT_ID(id)                                  \
	do {                                                                \
		if (DT_PROP(id, value) ==                                   \
		    cached_ssfc.CBI_SSFC_UNION_ENTRY_NAME(DT_PARENT(id))) { \
			UTIL_LISTIFY(DT_PROP_LEN(id, devices),              \
				     ALT_MOTION_SENSOR_INIT, id)            \
		}                                                           \
	} while (0);

#define SSFC_ALT_MOTION_SENSOR_INIT(inst) \
	SSFC_ALT_MOTION_SENSOR_INIT_ID(DT_DRV_INST(inst))

#define SSFC_INIT_DEFAULT_ID(id)                                               \
	do {                                                                   \
		if (DT_PROP(id, default)) {                                    \
			cached_ssfc.CBI_SSFC_UNION_ENTRY_NAME(DT_PARENT(id)) = \
			    DT_PROP(id, value);                                \
		}                                                              \
	} while (0);

#define SSFC_INIT_DEFAULT(inst) \
	SSFC_INIT_DEFAULT_ID(DT_DRV_INST(inst))

static void cbi_ssfc_init(void)
{
	if (cbi_get_ssfc(&cached_ssfc.raw_value) != EC_SUCCESS) {
		/* Default to values specified in DTS */
		DT_INST_FOREACH_STATUS_OKAY(SSFC_INIT_DEFAULT)
	}

	LOG_INF("Read CBI SSFC : 0x%08X \n", cached_ssfc.raw_value);
	/*
	 * Adjust the motion_sensors array as soon as possible to initialize
	 * correct sensors
	 */
	DT_INST_FOREACH_STATUS_OKAY(SSFC_ALT_MOTION_SENSOR_INIT)
}
DECLARE_HOOK(HOOK_INIT, cbi_ssfc_init, HOOK_PRIO_FIRST);
