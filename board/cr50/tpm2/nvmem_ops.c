/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Global.h"
#include "NV_fp.h"
#include "util.h"

void nvmem_wipe_cache(void)
{
	/*
	 * Inclusive list of NV indices not to be wiped out when invalidating
	 * the cache.
	 */
	const uint16_t whitelist_range[] = { 0x1007, 0x100b };

	NvSelectivelyInvalidateCache(whitelist_range);

	/*
	 * Wipe some confidential persistent data
	 */
	memset(&gp.ownerAuth, 0, sizeof(gp.ownerAuth));
	memset(&gp.endorsementAuth, 0, sizeof(gp.endorsementAuth));
	memset(&gp.lockoutAuth, 0, sizeof(gp.lockoutAuth));
	memset(&gp.EPSeed, 0, sizeof(gp.EPSeed));
	memset(&gp.SPSeed, 0, sizeof(gp.SPSeed));
	memset(&gp.PPSeed, 0, sizeof(gp.PPSeed));
	memset(&gp.phProof, 0, sizeof(gp.phProof));
	memset(&gp.shProof, 0, sizeof(gp.shProof));
	memset(&gp.ehProof, 0, sizeof(gp.ehProof));
}
