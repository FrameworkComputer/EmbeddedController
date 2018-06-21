/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pinweaver_tpm_imports.h>

#include <Global.h>
#include <InternalRoutines.h>
#include <dcrypto.h>
#include <util.h>
#include <console.h>

void get_storage_seed(void *buf, size_t *len)
{
	*len = MIN(*len, sizeof(gp.SPSeed));
	memcpy(buf, &gp.SPSeed, *len);
}

uint8_t get_current_pcr_digest(const uint8_t bitmask[2],
			       uint8_t sha256_of_selected_pcr[32])
{
	TPM2B_DIGEST pcr_digest;
	TPML_PCR_SELECTION selection;

	selection.count = 1;
	selection.pcrSelections[0].hash = TPM_ALG_SHA256;
	selection.pcrSelections[0].sizeofSelect = PCR_SELECT_MIN;
	memset(&selection.pcrSelections[0].pcrSelect, 0, PCR_SELECT_MIN);
	memcpy(&selection.pcrSelections[0].pcrSelect, bitmask, 2);

	PCRComputeCurrentDigest(TPM_ALG_SHA256, &selection, &pcr_digest);
	if (memcmp(&selection.pcrSelections[0].pcrSelect, bitmask, 2) != 0)
		return 1;

	memcpy(sha256_of_selected_pcr, &pcr_digest.b.buffer, 32);
	return 0;
}
