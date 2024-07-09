/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_ssfc.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Cache SSFC on init since we don't expect it to change in runtime */
static union dedede_cbi_ssfc cached_ssfc;
BUILD_ASSERT(sizeof(cached_ssfc) == sizeof(uint32_t));

static void cbi_ssfc_init(void)
{
	if (cbi_get_ssfc(&cached_ssfc.raw_value) != EC_SUCCESS)
		/* Default to 0 when CBI isn't populated */
		cached_ssfc.raw_value = 0;

	CPRINTS("Read CBI SSFC : 0x%04X", cached_ssfc.raw_value);
}
DECLARE_HOOK(HOOK_INIT, cbi_ssfc_init, HOOK_PRIO_FIRST);

enum ec_ssfc_base_sensor get_cbi_ssfc_base_sensor(void)
{
	return (enum ec_ssfc_base_sensor)cached_ssfc.base_sensor;
}

enum ec_ssfc_lid_sensor get_cbi_ssfc_lid_sensor(void)
{
	return (enum ec_ssfc_lid_sensor)cached_ssfc.lid_sensor;
}
