/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "tpm_manufacture.h"

#include "Global.h"
#include "NV_fp.h"
#include "Platform.h"
#include "TPM_Types.h"
#include "TpmBuildSwitches.h"
#include "tpm_types.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

#define EK_CERT_NV_START_INDEX  0x01C00000

int tpm_manufactured(void)
{
	uint32_t nv_ram_index;
	const uint32_t rsa_ek_nv_index = EK_CERT_NV_START_INDEX;
	const uint32_t ecc_ek_nv_index = EK_CERT_NV_START_INDEX + 1;

	/*
	 * If nvram_index (value written at NV RAM offset of zero) is all
	 * ones, or either endorsement certificate is not installed, consider
	 * the chip un-manufactured.
	 *
	 * Thus, wiping flash NV ram allows to re-manufacture the chip.
	 */
	_plat__NvMemoryRead(0, sizeof(nv_ram_index), &nv_ram_index);
	if ((nv_ram_index == ~0) ||
	    (NvIsUndefinedIndex(rsa_ek_nv_index) == TPM_RC_SUCCESS) ||
	    (NvIsUndefinedIndex(ecc_ek_nv_index) == TPM_RC_SUCCESS)) {
		CPRINTF("%s: NOT manufactured\n", __func__);
		return 0;
	}

	CPRINTF("%s: manufactured\n", __func__);
	return 1;
}
