/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_TPM_NVMEM_READ_H
#define __EC_BOARD_CR50_TPM_NVMEM_READ_H

enum tpm_read_rv {
	tpm_read_success,
	tpm_read_not_found,
	tpm_read_too_small
};

enum tpm_read_rv read_tpm_nvmem(uint16_t object_index,
				uint16_t object_size,
				void *obj_value);

#endif  /* ! __EC_BOARD_CR50_TPM_NVMEM_READ_H */
