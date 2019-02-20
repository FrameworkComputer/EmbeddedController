/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_TPM_NVMEM_OPS_H
#define __EC_BOARD_CR50_TPM_NVMEM_OPS_H

enum tpm_read_rv {
	tpm_read_success,
	tpm_read_not_found,
	tpm_read_too_small
};

enum tpm_write_rv {
	tpm_write_created,
	tpm_write_updated,
	tpm_write_fail
};

enum tpm_nv_hidden_object {
	TPM_HIDDEN_U2F_KEK,
	TPM_HIDDEN_U2F_KH_SALT,
};

enum tpm_read_rv read_tpm_nvmem(uint16_t object_index,
				uint16_t object_size,
				void *obj_value);

/*
 * The following functions must only be called from the TPM task,
 * and only after TPM initialization is complete (specifically,
 * after NvInitStatic).
 */

enum tpm_read_rv read_tpm_nvmem_hidden(uint16_t object_index,
				       uint16_t object_size,
				       void *obj_value);

enum tpm_write_rv write_tpm_nvmem_hidden(uint16_t object_index,
					 uint16_t object_size,
					 void *obj_value,
					 int commit);

#endif  /* ! __EC_BOARD_CR50_TPM_NVMEM_OPS_H */
