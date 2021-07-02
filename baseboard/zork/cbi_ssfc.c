/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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
static uint32_t cached_ssfc;

static void cbi_ssfc_init(void)
{
	if (cbi_get_ssfc(&cached_ssfc) != EC_SUCCESS)
		/* Default to 0 when CBI isn't populated */
		cached_ssfc = 0;

	CPRINTS("Read CBI SSFC : 0x%04X", cached_ssfc);
}
DECLARE_HOOK(HOOK_INIT, cbi_ssfc_init, HOOK_PRIO_FIRST);

enum ec_ssfc_base_gyro_sensor get_cbi_ssfc_base_sensor(void)
{
	return (cached_ssfc & SSFC_BASE_GYRO_MASK) >> SSFC_BASE_GYRO_OFFSET;
}

enum ec_ssfc_spkr_auto_mode get_cbi_ssfc_spkr_auto_mode(void)
{
	return (cached_ssfc & SSFC_SPKR_AUTO_MODE_MASK) >>
	       SSFC_SPKR_AUTO_MODE_OFFSET;
}

enum ec_ssfc_edp_phy_alt_tuning get_cbi_ssfc_edp_phy_alt_tuning(void)
{
	return (cached_ssfc & SSFC_EDP_PHY_ALT_TUNING_MASK) >>
		SSFC_EDP_PHY_ALT_TUNING_OFFSET;
}

enum ec_ssfc_c1_mux get_cbi_ssfc_c1_mux(void)
{
	return (cached_ssfc & SSFC_C1_MUX_MASK) >>
		SSFC_C1_MUX_OFFSET;
}
