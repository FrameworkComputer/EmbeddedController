/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This header declares the TPM manufacture related interface.
 * Individual boards are expected to provide implementations.
 */

#ifndef __CROS_EC_TPM_MANUFACTURE_H
#define __CROS_EC_TPM_MANUFACTURE_H

/* Returns non-zero if the TPM manufacture steps have been completed. */
int tpm_manufactured(void);

/* Codes for success and various manufacturing error conditions. */
enum manufacturing_status {
	mnf_success = 0,
	mnf_no_certs = 1,
	mnf_eps_decr = 2,
	mnf_bad_rsa_size = 3,
	mnf_bad_total_size = 4,
	mnf_bad_rsa_type = 5,
	mnf_bad_ecc_type = 6,
	mnf_hmac_mismatch = 7,
	mnf_rsa_proc = 8,
	mnf_ecc_proc = 9,
	mnf_store = 10,
	mnf_manufactured = 11,
	mnf_unverified_cert = 12,
};

enum manufacturing_status tpm_endorse(void);

#endif	/* __CROS_EC_TPM_MANUFACTURE_H */
