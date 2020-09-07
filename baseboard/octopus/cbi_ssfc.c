/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_ssfc.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

/****************************************************************************
 * Octopus CBI Second Source Factory Cache
 */

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Cache SSFC on init since we don't expect it to change in runtime */
static uint32_t cached_ssfc;

static void cbi_ssfc_init(void)
{
	if (cbi_get_ssfc(&cached_ssfc) != EC_SUCCESS)
		/* Default to 0 when CBI isn't populated */
		cached_ssfc = 0;

	CPRINTS("CBI SSFC: 0x%04X", cached_ssfc);
}
DECLARE_HOOK(HOOK_INIT, cbi_ssfc_init, HOOK_PRIO_FIRST);

enum ssfc_tcpc_p1 get_cbi_ssfc_tcpc_p1(void)
{
	return ((cached_ssfc & SSFC_TCPC_P1_MASK) >> SSFC_TCPC_P1_OFFSET);
}

enum ssfc_ppc_p1 get_cbi_ssfc_ppc_p1(void)
{
	return ((cached_ssfc & SSFC_PPC_P1_MASK) >> SSFC_PPC_P1_OFFSET);
}
