/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Global.h"
#include "NV_fp.h"

void nvmem_wipe_cache(void)
{
	/*
	 * Inclusive list of NV indices not to be wiped out when invalidating
	 * the cache.
	 */
	const uint16_t whitelist_range[] = { 0x1007, 0x100b };

	NvSelectivelyInvalidateCache(whitelist_range);
}
