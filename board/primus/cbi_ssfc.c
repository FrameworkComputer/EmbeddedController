/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_ssfc.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Cache SSFC on init since we don't expect it to change in runtime */
static union primus_cbi_ssfc cached_ssfc;
BUILD_ASSERT(sizeof(cached_ssfc) == sizeof(uint32_t));

void board_init_ssfc(void)
{
	if (cbi_get_ssfc(&cached_ssfc.raw_value) != EC_SUCCESS)
		/* Default to 0 when CBI isn't populated */
		cached_ssfc.raw_value = 0;

	CPRINTS("Read CBI SSFC : 0x%04X", cached_ssfc.raw_value);
}

enum ec_ssfc_trackpoint get_cbi_ssfc_trackpoint(void)
{
	return cached_ssfc.trackpoint;
}
