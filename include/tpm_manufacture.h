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
int tpm_endorse(void);

#endif	/* __CROS_EC_TPM_MANUFACTURE_H */
