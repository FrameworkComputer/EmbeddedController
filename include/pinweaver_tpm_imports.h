/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Compatibility layer between the TPM code and PinWeaver.
 *
 * This is needed because the headers for the TPM are not compatible with the
 * headers used by pinweaver.c. It also makes it easier to mock the
 * functionality derived from the TPM code.
 */

#ifndef __CROS_EC_INCLUDE_PINWEAVER_TPM_IMPORTS_H
#define __CROS_EC_INCLUDE_PINWEAVER_TPM_IMPORTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This is used to get the storage seed from the TPM implementation so
 * TPM_Clear() will break the keys used by PinWeaver so that any metadata
 * that persists on the machine storage is unusable by attackers.
 */
void get_storage_seed(void *buf, size_t *len);

/* Reads PCR values for indexes from bitmask and computes sha256 digest of
 * concatenated values. Populates the digest in |sha256_of_selected_pcr|.
 */
uint8_t get_current_pcr_digest(const uint8_t bitmask[2],
			       uint8_t sha256_of_selected_pcr[32]);

#ifdef __cplusplus
}
#endif

#endif  /* __CROS_EC_INCLUDE_PINWEAVER_TPM_IMPORTS_H */
